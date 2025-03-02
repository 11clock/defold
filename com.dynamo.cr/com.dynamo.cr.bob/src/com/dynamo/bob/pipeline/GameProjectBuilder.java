// Copyright 2020 The Defold Foundation
// Licensed under the Defold License version 1.0 (the "License"); you may not use
// this file except in compliance with the License.
//
// You may obtain a copy of the License, together with FAQs at
// https://www.defold.com/license
//
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.

package com.dynamo.bob.pipeline;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.io.StringReader;
import java.io.BufferedReader;
import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collection;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;

import org.apache.commons.io.FileUtils;
import org.apache.commons.io.FilenameUtils;
import org.apache.commons.io.IOUtils;

import com.dynamo.bob.Bob;
import com.dynamo.bob.Builder;
import com.dynamo.bob.BuilderParams;
import com.dynamo.bob.CompileExceptionError;
import com.dynamo.bob.CopyCustomResourcesBuilder;
import com.dynamo.bob.Platform;
import com.dynamo.bob.Project;
import com.dynamo.bob.ProtoBuilder;
import com.dynamo.bob.Task;
import com.dynamo.bob.Task.TaskBuilder;
import com.dynamo.bob.archive.ArchiveBuilder;
import com.dynamo.bob.archive.EngineVersion;
import com.dynamo.bob.archive.ManifestBuilder;
import com.dynamo.bob.fs.IResource;
import com.dynamo.bob.util.BobProjectProperties;
import com.dynamo.graphics.proto.Graphics.PlatformProfile;
import com.dynamo.graphics.proto.Graphics.TextureProfile;
import com.dynamo.graphics.proto.Graphics.TextureProfiles;
import com.dynamo.liveupdate.proto.Manifest.HashAlgorithm;
import com.dynamo.liveupdate.proto.Manifest.SignAlgorithm;
import com.dynamo.proto.DdfExtensions;

import com.dynamo.gameobject.proto.GameObject.PrototypeDesc;
import com.dynamo.gamesys.proto.MeshProto.MeshDesc;
import com.dynamo.gamesys.proto.ModelProto.Model;
import com.dynamo.gamesys.proto.TextureSetProto.TextureSet;
import com.dynamo.graphics.proto.Graphics.Cubemap;
import com.dynamo.graphics.proto.Graphics.ShaderDesc;
import com.dynamo.render.proto.Font.FontMap;
import com.dynamo.rig.proto.Rig.MeshSet;
import com.dynamo.rig.proto.Rig.Skeleton;

import com.google.protobuf.DescriptorProtos.FieldOptions;
import com.google.protobuf.Descriptors.FieldDescriptor;
import com.google.protobuf.GeneratedMessage;
import com.google.protobuf.Message;

@BuilderParams(name = "GameProjectBuilder", inExts = ".project", outExt = "", createOrder = 1000)
public class GameProjectBuilder extends Builder<Void> {

    private RandomAccessFile createRandomAccessFile(File handle) throws IOException {
        handle.deleteOnExit();
        RandomAccessFile file = new RandomAccessFile(handle, "rw");
        file.setLength(0);
        return file;
    }

    @Override
    public Task<Void> create(IResource input) throws IOException, CompileExceptionError {
        boolean nonStandardGameProjectFile = !project.getGameProjectResource().getAbsPath().equals(input.getAbsPath());
        if (nonStandardGameProjectFile) {
            throw new CompileExceptionError(input, -1, "Found non-standard game.project file: " + input.getPath());
        }

        // We currently don't have a file mapping with an input -> output for certain files
        // These should to be setup in the corresponding builder!
        ProtoBuilder.addMessageClass(".animationsetc", MeshSet.class);
        ProtoBuilder.addMessageClass(".cubemapc", Cubemap.class);
        ProtoBuilder.addMessageClass(".fontc", FontMap.class);
        ProtoBuilder.addMessageClass(".fpc", ShaderDesc.class);
        ProtoBuilder.addMessageClass(".vpc", ShaderDesc.class);
        ProtoBuilder.addMessageClass(".goc", PrototypeDesc.class);
        ProtoBuilder.addMessageClass(".meshc", MeshDesc.class);
        ProtoBuilder.addMessageClass(".meshsetc", MeshSet.class);
        ProtoBuilder.addMessageClass(".modelc", Model.class);
        ProtoBuilder.addMessageClass(".skeletonc", Skeleton.class);
        ProtoBuilder.addMessageClass(".texturesetc", TextureSet.class);

        boolean shouldPublish = project.option("liveupdate", "false").equals("true");
        project.createPublisher(shouldPublish);
        TaskBuilder<Void> builder = Task.<Void> newBuilder(this)
                .setName(params.name())
                .addInput(input)
                .addOutput(input.changeExt(".projectc").disableCache());

        for (IResource propertyFile : project.getPropertyFilesAsResources()) {
            builder.addInput(propertyFile);
        }

        if (project.option("archive", "false").equals("true")) {
            builder.addOutput(input.changeExt(".arci").disableCache());
            builder.addOutput(input.changeExt(".arcd").disableCache());
            builder.addOutput(input.changeExt(".dmanifest").disableCache());
            builder.addOutput(input.changeExt(".public.der").disableCache());
            for (IResource output : project.getPublisher().getOutputs(input)) {
                builder.addOutput(output);
            }
        }

        project.buildResource(input, CopyCustomResourcesBuilder.class);

        // Load texture profile message if supplied and enabled
        String textureProfilesPath = project.getProjectProperties().getStringValue("graphics", "texture_profiles");
        if (textureProfilesPath != null) {

            TextureProfiles.Builder texProfilesBuilder = TextureProfiles.newBuilder();
            IResource texProfilesInput = project.getResource(textureProfilesPath);
            if (!texProfilesInput.exists()) {
                throw new CompileExceptionError(input, -1, "Could not find supplied texture_profiles file: " + textureProfilesPath);
            }
            ProtoUtil.merge(texProfilesInput, texProfilesBuilder);

            // If Bob is building for a specific platform, we need to
            // filter out any platform entries not relevant to the target platform.
            // (i.e. we don't want win32 specific profiles lingering in android bundles)
            String targetPlatform = project.option("platform", "");

            List<TextureProfile> newProfiles = new LinkedList<TextureProfile>();
            for (int i = 0; i < texProfilesBuilder.getProfilesCount(); i++) {

                TextureProfile profile = texProfilesBuilder.getProfiles(i);
                TextureProfile.Builder profileBuilder = TextureProfile.newBuilder();
                profileBuilder.mergeFrom(profile);
                profileBuilder.clearPlatforms();

                // Take only the platforms that matches the target platform
                for (PlatformProfile platformProfile : profile.getPlatformsList()) {
                    if (Platform.matchPlatformAgainstOS(targetPlatform, platformProfile.getOs())) {
                        profileBuilder.addPlatforms(platformProfile);
                    }
                }

                newProfiles.add(profileBuilder.build());
            }

            // Update profiles list with new filtered one
            // Now it should only contain profiles with platform entries
            // relevant for the target platform...
            texProfilesBuilder.clearProfiles();
            texProfilesBuilder.addAllProfiles(newProfiles);


            // Add the current texture profiles to the project, since this
            // needs to be reachedable by the TextureGenerator.
            TextureProfiles textureProfiles = texProfilesBuilder.build();
            project.setTextureProfiles(textureProfiles);
        }

        for (Task<?> task : project.getTasks()) {
            for (IResource output : task.getOutputs()) {
                builder.addInput(output);
            }
        }

        return builder.build();
    }

    private void createArchive(Collection<String> resources, RandomAccessFile archiveIndex, RandomAccessFile archiveData, ManifestBuilder manifestBuilder, List<String> excludedResources, Path resourcePackDirectory) throws IOException, CompileExceptionError {
        Bob.verbose("GameProjectBuilder.createArchive\n");
        long tstart = System.currentTimeMillis();

        String root = FilenameUtils.concat(project.getRootDirectory(), project.getBuildDirectory());

        // When passing use vanilla lua, we want the Lua code as clear text
        boolean use_vanilla_lua = project.option("use-vanilla-lua", "false").equals("true");

        int resourcePadding = 4;
        String resourcePaddingStr = project.option("archive-resource-padding", null);
        if (resourcePaddingStr != null) {
            // It's already verified by bob, but we have to still parse it again
            try {
                resourcePadding = Integer.parseInt(resourcePaddingStr);
            } catch (Exception e) {
                throw new CompileExceptionError(String.format("Could not parse --archive-resource-padding='%s' into a valid integer", resourcePaddingStr), e);
            }
        }

        ArchiveBuilder archiveBuilder = new ArchiveBuilder(root, manifestBuilder, use_vanilla_lua ? false : true, resourcePadding);
        boolean doCompress = project.getProjectProperties().getBooleanValue("project", "compress_archive", true);

        HashMap<String, EnumSet<Project.OutputFlags>> outputs = project.getOutputs();
        for (String s : resources) {
            EnumSet<Project.OutputFlags> flags = outputs.get(s);
            boolean compress = (flags != null && flags.contains(Project.OutputFlags.UNCOMPRESSED)) ? false : doCompress;
            archiveBuilder.add(s, compress);
        }

        archiveBuilder.write(archiveIndex, archiveData, resourcePackDirectory, excludedResources);
        manifestBuilder.setArchiveIdentifier(archiveBuilder.getArchiveIndexHash());
        archiveIndex.close();
        archiveData.close();

        // Populate publisher with the resource pack
        for (File fhandle : (new File(resourcePackDirectory.toAbsolutePath().toString())).listFiles()) {
            if (fhandle.isFile()) {
                project.getPublisher().AddEntry(fhandle.getName(), fhandle);
            }
        }

        long tend = System.currentTimeMillis();
        Bob.verbose("GameProjectBuilder.createArchive took %f\n", (tend-tstart)/1000.0);
    }

    private static void findResources(Project project, Message node, Collection<String> resources) throws CompileExceptionError {
        List<FieldDescriptor> fields = node.getDescriptorForType().getFields();

        for (FieldDescriptor fieldDescriptor : fields) {
            FieldOptions options = fieldDescriptor.getOptions();
            FieldDescriptor resourceDesc = DdfExtensions.resource.getDescriptor();
            boolean isResource = (Boolean) options.getField(resourceDesc);
            Object value = node.getField(fieldDescriptor);
            if (value instanceof Message) {
                findResources(project, (Message) value, resources);
            } else if (value instanceof List) {
                @SuppressWarnings("unchecked")
                List<Object> list = (List<Object>) value;
                for (Object v : list) {
                    if (v instanceof Message) {
                        findResources(project, (Message) v, resources);
                    } else if (isResource && v instanceof String) {
                        findResources(project, project.getResource((String) v), resources);
                    }
                }
            } else if (isResource && value instanceof String) {
                findResources(project, project.getResource((String) value), resources);
            }
        }
    }

    /*  Adds unique resources to list 'resources'. Each resource should once occur
        once in the list regardless if the resource appears in several collections
        or collectionproxies.
    */
    private static void findResources(Project project, IResource resource, Collection<String> resources) throws CompileExceptionError {
        if (resource.getPath().equals("") || resources.contains(resource.output().getAbsPath())) {
            return;
        }

        resources.add(resource.output().getAbsPath());

        int i = resource.getPath().lastIndexOf(".");
        if (i == -1) {
            return;
        }
        String ext = resource.getPath().substring(i);

        if (!ProtoBuilder.supportsType(ext)) {
            return;
        }

        GeneratedMessage.Builder<?> builder = ProtoBuilder.newBuilder(ext);
        try {
            final byte[] content = resource.output().getContent();
            if(content == null) {
                throw new CompileExceptionError(resource, 0, "Unable to find resource " + resource.getPath());
            }
            builder.mergeFrom(content);
            Object message = builder.build();
            findResources(project, (Message) message, resources);
        } catch(CompileExceptionError e) {
            throw e;
        } catch(Exception e) {
            throw new RuntimeException(e);
        }
    }

    private static void buildResourceGraph(Project project, Message node, ResourceNode parentNode, Collection<String> visitedNodes) throws CompileExceptionError {
        List<FieldDescriptor> fields = node.getDescriptorForType().getFields();
        for (FieldDescriptor fieldDescriptor : fields) {
            FieldOptions options = fieldDescriptor.getOptions();
            FieldDescriptor resourceDesc = DdfExtensions.resource.getDescriptor();
            boolean isResource = (Boolean) options.getField(resourceDesc);
            Object value = node.getField(fieldDescriptor);
            if (value instanceof Message) {
                buildResourceGraph(project, (Message) value, parentNode, visitedNodes);
            } else if (value instanceof List) {
                @SuppressWarnings("unchecked")
                List<Object> list = (List<Object>) value;
                for (Object v : list) {
                    if (v instanceof Message) {
                        buildResourceGraph(project, (Message) v, parentNode, visitedNodes);
                    } else if (isResource && v instanceof String) {
                        buildResourceGraph(project, project.getResource((String) v), parentNode, visitedNodes);
                    }
                }
            } else if (isResource && value instanceof String) {
                buildResourceGraph(project, project.getResource((String) value), parentNode, visitedNodes);
            }
        }
    }

    /*  Build a graph of resources. The graph is later used when writing archive to disk
        to determine whether the resource should be bundled with the application or
        excluded with liveupdate. Since liveupdate works on collectionproxies a resource
        will appear as a single node per collectionproxy, but can still have a other nodes
        in other collections/collectionproxies.
    */
    private static void buildResourceGraph(Project project, IResource resource, ResourceNode parentNode, Collection<String> visitedNodes) throws CompileExceptionError {
        if (resource.getPath().equals("") || visitedNodes.contains(resource.output().getAbsPath())) {
            return;
        }

        if (resource.output().getPath().endsWith(".collectionproxyc")) {
            visitedNodes = new HashSet<String>();
        }

        visitedNodes.add(resource.output().getAbsPath());
        ResourceNode currentNode = new ResourceNode(resource.getPath(), resource.output().getAbsPath());
        parentNode.addChild(currentNode);

        int i = resource.getPath().lastIndexOf(".");
        if (i == -1) {
            return;
        }
        String ext = resource.getPath().substring(i);

        if (!ProtoBuilder.supportsType(ext)) {
            return;
        }

        GeneratedMessage.Builder<?> builder = ProtoBuilder.newBuilder(ext);
        try {
            final byte[] content = resource.output().getContent();
            if(content == null) {
                throw new CompileExceptionError(resource, 0, "Unable to find resource " + resource.getPath());
            }
            builder.mergeFrom(content);
            Object message = builder.build();
            buildResourceGraph(project, (Message) message, currentNode, visitedNodes);
        } catch(CompileExceptionError e) {
            throw e;
        } catch(Exception e) {
            throw new RuntimeException(e);
        }
    }

    public static HashSet<String> findResources(Project project, ResourceNode rootNode) throws CompileExceptionError {
        HashSet<String> resources = new HashSet<String>();

        if (project.option("keep-unused", "false").equals("true")) {

            // All outputs of the project should be considered resources
            for (String path : project.getOutputs().keySet()) {
                resources.add(path);
            }

        } else {

            // Root nodes to follow (default values from engine.cpp)
            for (String[] tuples : new String[][] { {"bootstrap", "main_collection", "/logic/main.collectionc"},
                                                    {"bootstrap", "render", "/builtins/render/default.renderc"},
                                                    {"bootstrap", "debug_init_script", null},
                                                    {"input", "game_binding", "/input/game.input_bindingc"},
                                                    {"input", "gamepads", "/builtins/input/default.gamepadsc"},
                                                    {"display", "display_profiles", "/builtins/render/default.display_profilesc"}}) {
                String path = project.getProjectProperties().getStringValue(tuples[0], tuples[1], tuples[2]);
                HashSet<String> visitedNodes = new HashSet<String>();
                if (path != null) {
                    findResources(project, project.getResource(path), resources);
                    buildResourceGraph(project, project.getResource(path), rootNode, visitedNodes);
                }
            }

        }

        // Custom resources
        String[] custom_resources = project.getProjectProperties().getStringValue("project", "custom_resources", "").split(",");
        for (String s : custom_resources) {
            s = s.trim();
            if (s.length() > 0) {
                ArrayList<String> paths = new ArrayList<String>();
                project.findResourcePaths(s, paths);
                for (String path : paths) {
                    IResource r = project.getResource(path);
                    resources.add(r.output().getAbsPath());
                }
            }
        }

        return resources;
    }

    private ManifestBuilder prepareManifestBuilder(ResourceNode rootNode, List<String> excludedResources) throws IOException {
        String projectIdentifier = project.getProjectProperties().getStringValue("project", "title", "<anonymous>");
        String supportedEngineVersionsString = project.getPublisher().getSupportedVersions();
        String privateKeyFilepath = project.getPublisher().getManifestPrivateKey();
        String publicKeyFilepath = project.getPublisher().getManifestPublicKey();

        ManifestBuilder manifestBuilder = new ManifestBuilder();
        manifestBuilder.setRoot(rootNode);
        manifestBuilder.setResourceHashAlgorithm(HashAlgorithm.HASH_SHA1);
        manifestBuilder.setSignatureHashAlgorithm(HashAlgorithm.HASH_SHA256);
        manifestBuilder.setSignatureSignAlgorithm(SignAlgorithm.SIGN_RSA);
        manifestBuilder.setProjectIdentifier(projectIdentifier);
        manifestBuilder.setExcludedResources(excludedResources);

        // If manifest signing keys are specified, use them instead of generating them.
        if (!privateKeyFilepath.isEmpty() && !publicKeyFilepath.isEmpty() ) {
            if (!Files.exists(Paths.get(privateKeyFilepath))) {
                String altPrivateKeyFilepath = Paths.get(project.getRootDirectory(), privateKeyFilepath).toString();
                if (!Files.exists(Paths.get(altPrivateKeyFilepath))) {
                    throw new IOException(String.format("Couldn't find private key at either: '%s' or '%s'", privateKeyFilepath, altPrivateKeyFilepath));
                }
                privateKeyFilepath = altPrivateKeyFilepath;
            }

            if (!Files.exists(Paths.get(publicKeyFilepath))) {
                String altPublicKeyFilepath = Paths.get(project.getRootDirectory(), publicKeyFilepath).toString();
                if (!Files.exists(Paths.get(altPublicKeyFilepath))) {
                    throw new IOException(String.format("Couldn't find public key at either: '%s' or '%s'", publicKeyFilepath, altPublicKeyFilepath));
                }
                publicKeyFilepath = altPublicKeyFilepath;

            }
        }

        // If loading supplied keys failed or none were supplied, generate them instead.
        if (privateKeyFilepath.isEmpty() || publicKeyFilepath.isEmpty()) {
            if (project.option("liveupdate", "false").equals("true")) {
                System.err.println("\nWarning! No public or private key for manifest signing set in liveupdate settings, generating keys instead.");
            }
            File privateKeyFileHandle = File.createTempFile("defold.private_", ".der");
            privateKeyFileHandle.deleteOnExit();

            File publicKeyFileHandle = File.createTempFile("defold.public_", ".der");
            publicKeyFileHandle.deleteOnExit();

            privateKeyFilepath = privateKeyFileHandle.getAbsolutePath();
            publicKeyFilepath = publicKeyFileHandle.getAbsolutePath();
            try {
                ManifestBuilder.CryptographicOperations.generateKeyPair(SignAlgorithm.SIGN_RSA, privateKeyFilepath, publicKeyFilepath);
            } catch (NoSuchAlgorithmException exception) {
                throw new IOException("Unable to create manifest, cannot create asymmetric keypair!");
            }

        }
        manifestBuilder.setPrivateKeyFilepath(privateKeyFilepath);
        manifestBuilder.setPublicKeyFilepath(publicKeyFilepath);

        manifestBuilder.addSupportedEngineVersion(EngineVersion.version);
        if (supportedEngineVersionsString != null) {
            String[] supportedEngineVersions = supportedEngineVersionsString.split("\\s*,\\s*");
            for (String supportedEngineVersion : supportedEngineVersions) {
                manifestBuilder.addSupportedEngineVersion(supportedEngineVersion.trim());
            }
        }

        return manifestBuilder;
    }

    // Used to transform an input game.project properties map to a game.projectc representation.
    // Can be used for doing build time properties conversion.
    static public void transformGameProjectFile(BobProjectProperties properties) throws IOException {
        // Remove project dependencies list for security.
        properties.remove("project", "dependencies");

        // Map deprecated 'variable_dt' to new settings resulting in same runtime behavior
        Boolean variableDt = properties.getBooleanValue("display", "variable_dt");
        if (variableDt != null && variableDt == true) {
            System.err.println("\nWarning! Setting 'variable_dt' in 'game.project' is deprecated. Disabling 'Vsync' and setting 'Frame cap' to 0 for equivalent behavior.");
            properties.putBooleanValue("display", "vsync", false);
            properties.putIntValue("display", "update_frequency", 0);
        }
    }

    @Override
    public void build(Task<Void> task) throws CompileExceptionError, IOException {
        FileInputStream archiveIndexInputStream = null;
        FileInputStream archiveDataInputStream = null;
        FileInputStream resourcePackInputStream = null;
        FileInputStream publicKeyInputStream = null;

        IResource input = task.input(0);

        BobProjectProperties properties = Project.loadProperties(input, project.getPropertyFiles());

        try {
            if (project.option("archive", "false").equals("true")) {
                ResourceNode rootNode = new ResourceNode("<AnonymousRoot>", "<AnonymousRoot>");
                HashSet<String> resources = findResources(project, rootNode);

                List<String> excludedResources = new ArrayList<String>();
                for (String excludedResource : project.getExcludedCollectionProxies()) {
                    excludedResources.add(excludedResource);
                }

                ManifestBuilder manifestBuilder = this.prepareManifestBuilder(rootNode, excludedResources);

                // Make sure we don't try to archive the .arci, .arcd, .projectc, .dmanifest, .resourcepack.zip, .public.der
                for (IResource resource : task.getOutputs()) {
                    resources.remove(resource.getAbsPath());
                }

                // Create output for the data archive
                String platform = project.option("platform", "generic");
                project.getPublisher().setPlatform(platform);
                File archiveIndexHandle = File.createTempFile("defold.index_", ".arci");
                RandomAccessFile archiveIndex = createRandomAccessFile(archiveIndexHandle);
                File archiveDataHandle = File.createTempFile("defold.data_", ".arcd");
                RandomAccessFile archiveData = createRandomAccessFile(archiveDataHandle);
                Path resourcePackDirectory = Files.createTempDirectory("defold.resourcepack_");
                createArchive(resources, archiveIndex, archiveData, manifestBuilder, excludedResources, resourcePackDirectory);

                // Create manifest
                byte[] manifestFile = manifestBuilder.buildManifest();

                // Write outputs to the build system
                // game.arci
                archiveIndexInputStream = new FileInputStream(archiveIndexHandle);
                task.getOutputs().get(1).setContent(archiveIndexInputStream);

                // game.arcd
                archiveDataInputStream = new FileInputStream(archiveDataHandle);
                task.getOutputs().get(2).setContent(archiveDataInputStream);

                // game.dmanifest
                task.getOutputs().get(3).setContent(manifestFile);

                // game.public.der
                publicKeyInputStream = new FileInputStream(manifestBuilder.getPublicKeyFilepath());
                task.getOutputs().get(4).setContent(publicKeyInputStream);

                // Add copy of game.dmanifest to be published with liveuodate resources
                File manifestFileHandle = new File(task.getOutputs().get(3).getAbsPath());
                String liveupdateManifestFilename = "liveupdate.game.dmanifest";
                File manifestTmpFileHandle = new File(FilenameUtils.concat(manifestFileHandle.getParent(), liveupdateManifestFilename));
                FileUtils.copyFile(manifestFileHandle, manifestTmpFileHandle);
                project.getPublisher().AddEntry(liveupdateManifestFilename, manifestTmpFileHandle);
                project.getPublisher().Publish();

                manifestTmpFileHandle.delete();
                File resourcePackDirectoryHandle = new File(resourcePackDirectory.toAbsolutePath().toString());
                if (resourcePackDirectoryHandle.exists() && resourcePackDirectoryHandle.isDirectory()) {
                    FileUtils.deleteDirectory(resourcePackDirectoryHandle);
                }

                List<InputStream> publisherOutputs = project.getPublisher().getOutputResults();
                for (int i = 0; i < publisherOutputs.size(); ++i) {
                    task.getOutputs().get(5 + i).setContent(publisherOutputs.get(i));
                    IOUtils.closeQuietly(publisherOutputs.get(i));
                }
            }

            transformGameProjectFile(properties);
            task.getOutputs().get(0).setContent(properties.serialize().getBytes());
        } finally {
            IOUtils.closeQuietly(archiveIndexInputStream);
            IOUtils.closeQuietly(archiveDataInputStream);
            IOUtils.closeQuietly(resourcePackInputStream);
            IOUtils.closeQuietly(publicKeyInputStream);
        }
    }
}
