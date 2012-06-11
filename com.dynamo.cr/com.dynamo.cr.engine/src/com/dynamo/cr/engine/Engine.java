package com.dynamo.cr.engine;

import java.io.IOException;
import java.net.URL;

import org.eclipse.core.runtime.FileLocator;
import org.eclipse.core.runtime.Plugin;
import org.osgi.framework.BundleContext;

import com.dynamo.cr.editor.core.EditorCorePlugin;

public class Engine extends Plugin {

    private static BundleContext context;

    static BundleContext getContext() {
        return context;
    }

    private static Engine plugin;

    public static Engine getDefault() {
        return plugin;
    }

    public String getEnginePath(String platform) {
        String ext = "";
        if (platform.equals("win32")) {
            ext = ".exe";
        }

        URL bundleUrl = getBundle().getEntry("/engine/" + platform + "/dmengine" + ext);
        URL fileUrl;
        try {
            fileUrl = FileLocator.toFileURL(bundleUrl);
            return fileUrl.getPath();
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    public String getEnginePath() {
        return getEnginePath(EditorCorePlugin.getPlatform());
    }

    @Override
    public void start(BundleContext bundleContext) throws Exception {
        super.start(bundleContext);
        Engine.context = bundleContext;
        plugin = this;
    }

    @Override
    public void stop(BundleContext bundleContext) throws Exception {
        super.stop(bundleContext);
        Engine.context = null;
        plugin = null;
    }

}
