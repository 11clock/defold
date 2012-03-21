package com.dynamo.cr.server;

import java.io.BufferedReader;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.lang.management.ManagementFactory;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.URL;
import java.net.URLDecoder;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Properties;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.inject.Inject;
import javax.management.MBeanServer;
import javax.management.ObjectName;
import javax.persistence.EntityManager;
import javax.persistence.EntityManagerFactory;
import javax.servlet.ServletContextEvent;
import javax.servlet.ServletException;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;
import javax.ws.rs.core.Response.Status;

import org.eclipse.jetty.http.HttpHeaders;
import org.eclipse.jetty.http.HttpStatus;
import org.eclipse.jetty.server.Request;
import org.eclipse.jetty.server.bio.SocketConnector;
import org.eclipse.jetty.server.handler.HandlerList;
import org.eclipse.jetty.server.handler.ResourceHandler;
import org.eclipse.jetty.util.resource.Resource;
import org.eclipse.jgit.http.server.GitServlet;
import org.eclipse.persistence.config.PersistenceUnitProperties;
import org.glassfish.grizzly.http.server.HttpServer;
import org.glassfish.grizzly.servlet.ServletHandler;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.joran.JoranConfigurator;
import ch.qos.logback.core.joran.spi.JoranException;
import ch.qos.logback.core.util.StatusPrinter;

import com.dynamo.cr.branchrepo.BranchRepository;
import com.dynamo.cr.proto.Config.Configuration;
import com.dynamo.cr.proto.Config.InvitationCountEntry;
import com.dynamo.cr.protocol.proto.Protocol.BuildDesc;
import com.dynamo.cr.protocol.proto.Protocol.BuildDesc.Activity;
import com.dynamo.cr.protocol.proto.Protocol.BuildLog;
import com.dynamo.cr.protocol.proto.Protocol.LaunchInfo;
import com.dynamo.cr.protocol.proto.Protocol.Log;
import com.dynamo.cr.server.auth.GitSecurityFilter;
import com.dynamo.cr.server.auth.OpenIDAuthenticator;
import com.dynamo.cr.server.auth.SecurityFilter;
import com.dynamo.cr.server.mail.IMailProcessor;
import com.dynamo.cr.server.model.InvitationAccount;
import com.dynamo.cr.server.model.ModelUtil;
import com.dynamo.cr.server.model.Project;
import com.dynamo.cr.server.model.User;
import com.dynamo.cr.server.model.User.Role;
import com.dynamo.cr.server.openid.OpenID;
import com.dynamo.cr.server.resources.LoginResource;
import com.dynamo.cr.server.resources.ProjectResource;
import com.dynamo.cr.server.resources.ProjectsResource;
import com.dynamo.cr.server.resources.RepositoryResource;
import com.dynamo.cr.server.resources.ResourceUtil;
import com.dynamo.cr.server.resources.UsersResource;
import com.dynamo.inject.persist.PersistFilter;
import com.dynamo.inject.persist.jpa.JpaPersistModule;
import com.dynamo.server.dgit.GitFactory;
import com.dynamo.server.dgit.GitFactory.Type;
import com.dynamo.server.dgit.IGit;
import com.google.inject.Guice;
import com.google.inject.Injector;
import com.google.inject.servlet.GuiceFilter;
import com.google.inject.servlet.GuiceServletContextListener;
import com.google.inject.servlet.ServletModule;
import com.sun.jersey.api.container.filter.RolesAllowedResourceFilterFactory;
import com.sun.jersey.guice.spi.container.servlet.GuiceContainer;
import com.sun.jersey.spi.container.servlet.ServletContainer;

public class Server implements ServerMBean {

    protected static Logger logger = LoggerFactory.getLogger(Server.class);

    private HttpServer httpServer;
    private String baseUri;
    private Pattern[] filterPatterns;
    private Configuration configuration;
    private EntityManagerFactory emf;
    private org.eclipse.jetty.server.Server jettyServer;
    private ETagCache etagCache = new ETagCache(10000);
    private static final int MAX_ACTIVE_LOGINS = 1024;
    private OpenID openID = new OpenID();
    private OpenIDAuthenticator openIDAuthentication = new OpenIDAuthenticator(MAX_ACTIVE_LOGINS);
    private SecureRandom secureRandom;
    private BranchRepository branchRepository;

    private IMailProcessor mailProcessor;

    private int cleanupBuildsInterval = 10 * 1000; // 10 seconds
    private int keepBuildDescFor = 100 * 1000; // 100 seconds
    private int nextBuildNumber = 0;
    private Map<Integer, RuntimeBuildDesc> builds = new HashMap<Integer, RuntimeBuildDesc>();

    private CleanBuildsThread cleanupThread;

    private ExecutorService executorService;

    class CleanBuildsThread extends Thread {
        private boolean quit = false;

        public CleanBuildsThread() {
            setName("Cleanup builds thread");
        }

        @Override
        public void run() {
            while (!quit) {
                try {
                    Thread.sleep(cleanupBuildsInterval);

                    Set<Integer> toRemove = new HashSet<Integer>();
                    synchronized (builds) {
                        for (Integer id : builds.keySet()) {
                            RuntimeBuildDesc buildDesc = builds.get(id);
                            long time = System.currentTimeMillis();
                            if (buildDesc.activity == Activity.IDLE && time > (buildDesc.buildCompleted + keepBuildDescFor)) {
                                toRemove.add(id);
                            }
                        }
                        for (Integer id : toRemove) {
                            logger.info("Removing build {}", id);
                            builds.remove(id);
                        }
                    }
                } catch (InterruptedException e) {
                }
            }
        }

        public void quit() {
            this.quit = true;
        }
    }

    /**
     * Clean up for old builds thread wake-up interval
     * @param cleanupBuildsInterval interval in ms
     */
    public void setCleanupBuildsInterval(int cleanupBuildsInterval) {
        this.cleanupBuildsInterval = cleanupBuildsInterval;
        this.cleanupThread.interrupt();
    }

    /**
     * Set time to keep old builds
     * @param keepBuildDescFor time to keep completed builds for in ms
     */
    public void setKeepBuildDescFor(int keepBuildDescFor) {
        this.keepBuildDescFor = keepBuildDescFor;
    }

    public static class Config extends GuiceServletContextListener {

        private Server server;

        public Config(Server server) {
            this.server = server;
        }

        @Override
        protected Injector getInjector() {
            return Guice.createInjector(new ServletModule(){
                @Override
                protected void configureServlets() {
                    Properties props = new Properties();
                    props.put(PersistenceUnitProperties.CLASSLOADER, server.getClass().getClassLoader());
                    install(new JpaPersistModule("unit-test").properties(props));

                    bind(Server.class).toInstance(server);
                    bind(BranchRepository.class).toInstance(server.branchRepository);

                    bind(RepositoryResource.class);
                    bind(ProjectResource.class);
                    bind(ProjectsResource.class);
                    bind(UsersResource.class);
                    bind(LoginResource.class);

                    Map<String, String> params = new HashMap<String, String>();
                    params.put("com.sun.jersey.config.property.resourceConfigClass",
                            "com.dynamo.cr.server.ResourceConfig");

                    params.put(ResourceConfig.PROPERTY_CONTAINER_REQUEST_FILTERS,
                            PreLoggingRequestFilter.class.getName() +  ";" + SecurityFilter.class.getName());
                    params.put(ResourceConfig.PROPERTY_RESOURCE_FILTER_FACTORIES,
                            RolesAllowedResourceFilterFactory.class.getName());
                    params.put(ResourceConfig.PROPERTY_CONTAINER_RESPONSE_FILTERS,
                            CrossSiteHeaderResponseFilter.class.getName() + ";" + PostLoggingResponseFilter.class.getName());

                    filter("*").through(PersistFilter.class);
                    serve("*").with(GuiceContainer.class, params);
                }
            });
        }
    }

    public class GuiceHandler extends ServletHandler {
        private final GuiceServletContextListener guiceServletContextListener;

        public GuiceHandler(Config guiceServletContextListener) {
            this.guiceServletContextListener = guiceServletContextListener;
        }

        @Override
        public void start() {
            guiceServletContextListener.contextInitialized(new ServletContextEvent(getServletCtx()));
            super.start();
        }

        @Override
        public void destroy() {
            super.destroy();
            guiceServletContextListener.contextDestroyed(new ServletContextEvent(getServletCtx()));
        }
    }

    HttpServer createHttpServer() throws IOException {
        HttpServer server = HttpServer.createSimpleServer("/", configuration.getServicePort());
        Config config = new Config(this);
        ServletHandler handler = new GuiceHandler(config);
        handler.addFilter(new GuiceFilter(), "GuiceFilter", null);
        handler.setServletInstance(new ServletContainer());

        /*
         * NOTE:
         * Class scanning in jersey with "com.sun.jersey.config.property.packages"
         * is not compatible with OSGi.
         * Therefore are all resource classes specified in class ResourceConfig
         * using  "com.sun.jersey.config.property.resourceConfigClass" instead
         *
         * Old "code":
         * initParams.put("com.sun.jersey.config.property.packages", "com.dynamo.cr.server.resources;com.dynamo.cr.server.auth;com.dynamo.cr.common.providers");
         */

        handler.addInitParameter("com.sun.jersey.config.property.resourceConfigClass",
                "com.dynamo.cr.server.ResourceConfig");

        handler.addInitParameter(ResourceConfig.PROPERTY_CONTAINER_REQUEST_FILTERS,
                PreLoggingRequestFilter.class.getName() +  ";" + SecurityFilter.class.getName());
        handler.addInitParameter(ResourceConfig.PROPERTY_RESOURCE_FILTER_FACTORIES,
                RolesAllowedResourceFilterFactory.class.getName());
        handler.addInitParameter(ResourceConfig.PROPERTY_CONTAINER_RESPONSE_FILTERS,
                CrossSiteHeaderResponseFilter.class.getName() + ";" + PostLoggingResponseFilter.class.getName());

        server.getServerConfiguration().addHttpHandler(handler, "/*");
        server.start();
        return server;
    }

    public Server() {}

    @Inject
    public Server(EntityManagerFactory emf,
                  Configuration configuration,
                  IMailProcessor mailProcessor) throws IOException {

        this.emf = emf;
        this.configuration = configuration;
        this.mailProcessor = mailProcessor;

        LoggerContext lc = (LoggerContext) LoggerFactory.getILoggerFactory();

        // Configure logback logging
        // The logback jars are not located in the current bundle. Hence logback.xml
        // is not found. Configure logback explicitly
        try {
          JoranConfigurator configurator = new JoranConfigurator();
          configurator.setContext(lc);
          // the context was probably already configured by default configuration rules
          lc.reset();
          URL url = this.getClass().getClassLoader().getResource("/logback.xml");
          configurator.doConfigure(url);
        } catch (JoranException je) {
           je.printStackTrace();
        }
        StatusPrinter.printInCaseOfErrorsOrWarnings(lc);

        try {
            secureRandom = SecureRandom.getInstance("SHA1PRNG");
        } catch (NoSuchAlgorithmException e1) {
            throw new RuntimeException(e1);
        }

        this.cleanupThread = new CleanBuildsThread();
        this.cleanupThread.start();

        filterPatterns = new Pattern[configuration.getFiltersCount()];
        int i = 0;
        for (String f : configuration.getFiltersList()) {
            filterPatterns[i++] = Pattern.compile(f);
        }
        bootStrapUsers();

        String builtinsDirectory = null;
        if (configuration.hasBuiltinsDirectory())
            builtinsDirectory = configuration.getBuiltinsDirectory();
        branchRepository = new RemoteBranchRepository(this, configuration.getBranchRoot(), configuration.getRepositoryRoot(), builtinsDirectory, filterPatterns);

        baseUri = String.format("http://0.0.0.0:%d/", this.configuration.getServicePort());

        httpServer = createHttpServer();
        GitServlet gitServlet = new GitServlet();
        ServletHandler gitHandler = new ServletHandler(gitServlet);
        gitHandler.addFilter(new GitSecurityFilter(emf), "gitAuth", null);

        /*
         * NOTE: The GitServlet and friends seems to be a bit budget
         * If the servlet is mapped to http://host/git a folder "git"
         * must be present in the base-path, eg /tmp/git/... if the base-path
         * is /tmp
         */

        /*
         * NOTE: http.recivepack must be enabled in the server-side config, eg
         *
         * [http]
         *        receivepack = true
         *
         */

        String basePath = ResourceUtil.getGitBasePath(getConfiguration());
        logger.info("git base-path: {}", basePath);
        gitHandler.addInitParameter("base-path", basePath);
        gitHandler.addInitParameter("export-all", "1");

        String baseUri = ResourceUtil.getGitBaseUri(getConfiguration());
        logger.info("git base uri: {}", baseUri);
        httpServer.getServerConfiguration().addHttpHandler(gitHandler, baseUri);

        if (!GitFactory.create(Type.CGIT).checkGitVersion()) {
            // TODO: Hmm, exception...
            throw new RuntimeException("Invalid version of git or not found");
        }

        if (configuration.getDataServerEnabled() != 0) {
            initDataServer();
        }

        this.mailProcessor.start();

        this.executorService = Executors.newSingleThreadExecutor();

        MBeanServer mbs = ManagementFactory.getPlatformMBeanServer();
        ObjectName name;
        try {
            name = new ObjectName("com.dynamo:type=Server");
            if (!mbs.isRegistered(name)) {
                mbs.registerMBean(this, name);
            }
        } catch (Throwable e) {
            logger.error(e.getMessage(), e);
        }
    }

    public ExecutorService getExecutorService() {
        return executorService;
    }

    public void initDataServer() throws IOException {
        jettyServer = new org.eclipse.jetty.server.Server();

        SocketConnector connector = new SocketConnector();
        connector.setPort(configuration.getBuildDataPort());
        jettyServer.addConnector(connector);
        HandlerList handlerList = new HandlerList();
        ResourceHandler resourceHandler = new ResourceHandler() {

            @Override
            public void handle(String target, Request baseRequest, HttpServletRequest request, HttpServletResponse response) throws IOException, ServletException {
                if (baseRequest.isHandled())
                    return;

                if (target.equals("/__verify_etags__")) {
                    baseRequest.setHandled(true);

                    if (!request.getMethod().equals("POST")) {
                        response.setStatus(HttpServletResponse.SC_BAD_REQUEST );
                        return;
                    }

                    StringBuffer responseBuffer = new StringBuffer();
                    BufferedReader reader = new BufferedReader(new InputStreamReader(request.getInputStream()));

                    try {
                        String line = reader.readLine();
                        while (line != null) {
                            int i = line.indexOf(' ');
                            URI uri;
                            try {
                                uri = new URI(URLDecoder.decode(line.substring(0, i), "UTF-8"));
                                uri = uri.normalize(); // http://foo.com//a -> http://foo.com/a
                            } catch (URISyntaxException e) {
                                logger.warn(e.getMessage(), e);
                                continue;
                            }

                            String etag = line.substring(i + 1);
                            Resource resource = getResource(uri.getPath());
                            if (resource != null && resource.exists() && resource.getFile() != null) {
                                String thisEtag = etagCache.getETag(resource.getFile());
                                if (etag.equals(thisEtag)) {
                                    responseBuffer.append(line.substring(0, i));
                                    responseBuffer.append('\n');
                                }
                            }
                            else {
                                logger.warn("File doesn't exists {}", uri.getPath());
                            }

                            line = reader.readLine();
                        }
                    } finally {
                        reader.close();
                    }

                    response.getWriter().print(responseBuffer);
                    response.setStatus(HttpServletResponse.SC_OK);
                    return;
                }

                String ifNoneMatch = request.getHeader(HttpHeaders.IF_NONE_MATCH);
                if (ifNoneMatch != null) {
                    Resource resource = getResource(request);
                    if (resource != null && resource.exists()) {
                        File file = resource.getFile();
                        if (file != null) {
                            String thisEtag = etagCache.getETag(file);
                            if (thisEtag != null && ifNoneMatch.equals(thisEtag)) {
                                baseRequest.setHandled(true);
                                response.setHeader(HttpHeaders.ETAG, thisEtag);
                                response.setStatus(HttpStatus.NOT_MODIFIED_304);
                                return;
                            }
                        }
                    }
                }

                super.handle(target, baseRequest, request, response);
            }

            @Override
            protected void doResponseHeaders(HttpServletResponse response,
                    Resource resource,
                    String mimeType) {
                super.doResponseHeaders(response, resource, mimeType);
                try {
                    File file = resource.getFile();
                    if (file != null) {
                        String etag = etagCache.getETag(file);
                        if (etag != null) {
                            response.setHeader(HttpHeaders.ETAG, etag);
                        }
                    }
                }
                catch(IOException e) {
                    throw new RuntimeException(e);
                }
            }
        };
        resourceHandler.setResourceBase(configuration.getBranchRoot());
        handlerList.addHandler(resourceHandler);
        jettyServer.setHandler(handlerList);

        try {
            jettyServer.start();
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    public IMailProcessor getMailProcessor() {
        return mailProcessor;
    }

    public SecureRandom getSecureRandom() {
        return secureRandom;
    }

    public OpenID getOpenID() {
        return openID;
    }

    public OpenIDAuthenticator getOpenIDAuthentication() {
        return openIDAuthentication;
    }

    @Override
    public int getETagCacheHits() {
        return etagCache.getCacheHits();
    }

    @Override
    public int getETagCacheMisses() {
        return etagCache.getCacheMisses();
    }

    @Override
    public int getResourceDataRequests() {
        return branchRepository.getResourceDataRequests();
    }

    @Override
    public int getResourceInfoRequests() {
        return branchRepository.getResourceInfoRequests();
    }

    private void bootStrapUsers() {
        EntityManager em = emf.createEntityManager();
        em.getTransaction().begin();
        String[][] users = new String[][] {
                { "dynamogameengine@gmail.com", "admin", "Mr", "Admin" } };

        for (String[] entry : users) {
            if (ModelUtil.findUserByEmail(em, entry[0]) == null) {
                User u = new User();
                u.setEmail(entry[0]);
                u.setFirstName(entry[2]);
                u.setLastName(entry[3]);
                if (entry[1].equals("admin")) {
                    u.setPassword("mobEpeyfUj9");
                    u.setRole(Role.ADMIN);
                } else {
                    u.setPassword(entry[1]);
                }
                em.persist(u);
            }
        }
        em.getTransaction().commit();
        em.close();
    }

    public void stop() {
        mailProcessor.stop();

        if (this.cleanupThread != null) {
            this.cleanupThread.quit();
            this.cleanupThread.interrupt();
            try {
                this.cleanupThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }

        httpServer.stop();
        if (jettyServer != null) {
            try {
                jettyServer.stop();
            } catch (Exception e) {
                logger.error(e.getMessage(), e);
            }
        }
        emf.close();
    }

    public Project getProject(EntityManager em, String id) throws ServerException {
        Project project = em.find(Project.class, Long.parseLong(id));
        if (project == null)
            throw new ServerException(String.format("No such project %s", project));

        return project;
    }

    public User getUser(EntityManager em, String userId) throws ServerException {
        User user = em.find(User.class, Long.parseLong(userId));
        if (user == null)
            throw new ServerException(String.format("No such user %s", userId), javax.ws.rs.core.Response.Status.NOT_FOUND);

        return user;
    }

    public InvitationAccount getInvitationAccount(EntityManager em, String userId) throws ServerException {
        InvitationAccount invitationAccount = em.find(InvitationAccount.class, Long.parseLong(userId));
        if (invitationAccount == null)
            throw new ServerException(String.format("No such invitation account %s", userId), javax.ws.rs.core.Response.Status.NOT_FOUND);

        return invitationAccount;
    }

    public int getInvitationCount(int originalCount) {
        for (InvitationCountEntry entry : this.configuration.getInvitationCountMapList()) {
            if (entry.getKey() == originalCount) {
                return entry.getValue();
            }
        }
        return 0;
    }

    public String getBaseURI() {
        return baseUri;
    }

    public Log log(EntityManager em, String project, int maxCount) throws IOException, ServerException {
        getProject(em, project);
        String sourcePath = String.format("%s/%s", configuration.getRepositoryRoot(), project);
        IGit git = GitFactory.create(Type.CGIT);
        return git.log(sourcePath, maxCount);
    }

    static class BuildRunnable implements IBuildRunnable {

        private int id;
        private Server server;
        private String branchRoot;
        private String project;
        private String user;
        private String branch;
        private Pattern workPattern;
		private boolean rebuild;
        private Configuration configuration;
        private StringBuffer stdOut;
        private StringBuffer stdErr;
        private boolean cancel = false;

        public BuildRunnable(Server server, int id, String branch_root, String project, String user, String branch, boolean rebuild) {
            this.id = id;
            this.server = server;
            this.branchRoot = branch_root;
            this.project = project;
            this.user = user;
            this.branch = branch;
            this.rebuild = rebuild;
            this.configuration = server.configuration;
            workPattern = Pattern.compile(server.configuration.getProgressRe());
        }

        private void substitute(String[] args) {
            String cwd = System.getProperty("user.dir");
            for (int i = 0; i < args.length; ++i) {
                args[i] = args[i].replace("{cwd}", cwd);
            }
        }

        private int execCommand(String working_dir, String[] command) throws IOException {
            this.stdOut = new StringBuffer();
            this.stdErr = new StringBuffer();

            ProcessBuilder pb = new ProcessBuilder(command);
            if (working_dir != null)
                pb.directory(new File(working_dir));

            Process process = pb.start();
            InputStream std = process.getInputStream();
            InputStream err = process.getErrorStream();

            int exitValue = 0;

            boolean done = false;
            while (!done) {
                if (cancel) {
                    process.destroy();
                    return 1;
                }

                try {
                    exitValue = process.exitValue();
                    done = true;
                } catch (IllegalThreadStateException e) {
                    try {
                        Thread.sleep(100);
                    } catch (InterruptedException e1) {
                        logger.error(e.getMessage(), e);
                    }
                }

                while (std.available() > 0) {
                    byte[] tmp = new byte[std.available()];
                    std.read(tmp);
                    stdOut.append(new String(tmp));
                }

                while (err.available() > 0) {
                    byte[] tmp = new byte[err.available()];
                    err.read(tmp);
                    stdErr.append(new String(tmp));
                    updateProgressStatus();
                }
            }

            std.close();
            err.close();
            try {
                exitValue = process.waitFor();
            } catch (InterruptedException e) {
                logger.error(e.getMessage(), e);
            }

            return exitValue;
        }

        @Override
        public void cancel() {
            this.cancel = true;
        }

        @Override
        public void run() {
            try {
                doRun();
            } catch (Throwable e) {
                this.server.updateBuild(id, Activity.IDLE, BuildDesc.Result.ERROR, "", "Internal error");
                logger.error("Unexpected excetion caught", e);
            }
        }

        public void doRun() {
            String p = String.format("%s/%s/%s/%s", branchRoot, project, user, branch);
            try {
                String[] args = new String[configuration.getConfigureCommand().getArgsCount()];

                configuration.getConfigureCommand().getArgsList().toArray(args);
                substitute(args);

                int exitValue = execCommand(p, args);
                if (exitValue != 0) {
                    this.server.updateBuild(id, Activity.IDLE, BuildDesc.Result.ERROR, stdOut.toString(), stdErr.toString());
                    return;
                }

                if (rebuild) {
                    args = new String[configuration.getRebuildCommand().getArgsCount()];
                    configuration.getRebuildCommand().getArgsList().toArray(args);
                    substitute(args);
                } else {
                    args = new String[configuration.getBuildCommand().getArgsCount()];
                    configuration.getBuildCommand().getArgsList().toArray(args);
                    substitute(args);
                }
                exitValue = execCommand(p, args);
                if (exitValue != 0) {
                    this.server.updateBuild(id, Activity.IDLE, BuildDesc.Result.ERROR, stdOut.toString(), stdErr.toString());
                    return;
                }

                this.server.updateBuild(id, Activity.IDLE, BuildDesc.Result.OK, stdOut.toString(), stdErr.toString());
            } catch (IOException e) {
                logger.warn(e.getMessage(), e);
            }
        }

        public void updateProgressStatus() {
            Matcher m = workPattern.matcher(stdErr.toString());

            int progress = -1;
            int workAmount = -1;
            while (m.find()) {
                progress = Integer.valueOf(m.group(1));
                workAmount = Integer.valueOf(m.group(2));
            }

            if (progress != -1) {
                server.updateBuildProgress(id, progress, workAmount);
            }
        }
    }

    public BuildDesc build(String project, String user, String branch, boolean rebuild) throws ServerException {
        int build_number;
        synchronized(this) {

            for (Integer id : builds.keySet()) {
                RuntimeBuildDesc rtbd = builds.get(id);
                if (rtbd.project.equals(project) && rtbd.user.equals(user) && rtbd.branch.equals(branch) && rtbd.activity == Activity.BUILDING) {
                    // Build in progress
                    throw new ServerException("Build already in progress", Status.CONFLICT);
                }
            }

            build_number = nextBuildNumber++;
        }

        BuildDesc bd = BuildDesc.newBuilder()
            .setId(build_number)
            .setBuildActivity(Activity.BUILDING)
            .setBuildResult(BuildDesc.Result.OK)
            .setProgress(-1)
            .setWorkAmount(-1)
            .build();

        RuntimeBuildDesc rtbd = new RuntimeBuildDesc();
        rtbd.id = build_number;
        rtbd.project = project;
        rtbd.user = user;
        rtbd.branch = branch;

        rtbd.activity = Activity.BUILDING;
        rtbd.buildResult = BuildDesc.Result.OK;
        synchronized (builds) {
            builds.put(build_number, rtbd);
        }

        rtbd.buildRunnable = new BuildRunnable(this, build_number, branchRepository.getBranchRoot(), project, user, branch, rebuild);
        rtbd.buildThread = new Thread(rtbd.buildRunnable);
        rtbd.buildThread.start();

        return bd;
    }

    public synchronized void updateBuild(int id, Activity activity, BuildDesc.Result result, String std_out, String std_err) {
        RuntimeBuildDesc rtbd;
        synchronized (builds) {
             rtbd = builds.get(id);
        }

        if (activity == Activity.BUILDING && rtbd.activity == Activity.IDLE) {
            throw new RuntimeException("Invalid transiation, IDLE to BUILDING");
        }

        rtbd.activity = activity;
        rtbd.buildResult = result;
        rtbd.stdOut = std_out;
        rtbd.stdErr = std_err;
        if (activity == Activity.IDLE) {
            rtbd.buildCompleted = System.currentTimeMillis();
        }
    }

    private synchronized void updateBuildProgress(int id, int progress, int workAmount) {
        RuntimeBuildDesc rtbd;
        synchronized (builds) {
            rtbd = builds.get(id);
        }
        rtbd.progress = progress;
        rtbd.workAmount = workAmount;
    }

    public BuildDesc buildStatus(String project, String user, String branch, int id) throws ServerException {
        RuntimeBuildDesc rtbd;
        synchronized (builds) {
            rtbd = builds.get(id);
        }

        if (rtbd == null) {
            throw new ServerException(String.format("Build %d not found", id), Status.NOT_FOUND);
        }

        BuildDesc bd = BuildDesc.newBuilder()
            .setId(rtbd.id)
            .setBuildActivity(rtbd.activity)
            .setBuildResult(rtbd.buildResult)
            .setProgress(rtbd.progress)
            .setWorkAmount(rtbd.workAmount)
            .build();

        return bd;
    }

    public void cancelBuild(String project, String user, String branch, int id) throws ServerException {
        RuntimeBuildDesc rtbd;
        synchronized (builds) {
            rtbd = builds.get(id);
        }

        if (rtbd == null) {
            throw new ServerException(String.format("Build %d not found", id), Status.NOT_FOUND);
        } else {
            rtbd.buildRunnable.cancel();
            try {
                rtbd.buildThread.join(5000);
            } catch (InterruptedException e) {
                logger.error("Unexpected interrupted exception", e);
            }
        }
    }

    public BuildLog buildLog(String project, String user, String branch, int id) {
        RuntimeBuildDesc rtbd;
        synchronized (builds) {
            rtbd = builds.get(id);
        }
        return BuildLog.newBuilder()
            .setStdOut(rtbd.stdOut)
            .setStdErr(rtbd.stdErr).build();
    }

    public LaunchInfo getLaunchInfo(EntityManager em, String project) throws ServerException {
        // We check that the project really exists here
        getProject(em, project);

        LaunchInfo.Builder b = LaunchInfo.newBuilder();
        for (String s : configuration.getExeCommand().getArgsList()) {
            b.addArgs(s);
        }

        return b.build();
    }

    public EntityManagerFactory getEntityManagerFactory() {
        return emf;
    }

    public Configuration getConfiguration() {
        return configuration;
    }

    public String getBranchRoot() {
        return branchRepository.getBranchRoot();
    }


}
