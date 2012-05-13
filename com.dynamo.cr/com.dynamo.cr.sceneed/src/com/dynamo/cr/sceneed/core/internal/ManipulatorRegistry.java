package com.dynamo.cr.sceneed.core.internal;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eclipse.core.runtime.IConfigurationElement;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Platform;
import org.eclipse.core.runtime.Plugin;
import org.eclipse.core.runtime.Status;

import com.dynamo.cr.sceneed.Activator;
import com.dynamo.cr.sceneed.core.IManipulatorInfo;
import com.dynamo.cr.sceneed.core.IManipulatorMode;
import com.dynamo.cr.sceneed.core.IManipulatorRegistry;
import com.dynamo.cr.sceneed.core.INodeType;
import com.dynamo.cr.sceneed.core.INodeTypeRegistry;
import com.dynamo.cr.sceneed.ui.RootManipulator;

public class ManipulatorRegistry implements IManipulatorRegistry {

    Map<String, ManipulatorMode> modes = new HashMap<String, ManipulatorMode>();
    private Plugin plugin;

    public void init(Plugin plugin) {
        this.plugin = plugin;
        IConfigurationElement[] config = Platform.getExtensionRegistry()
                .getConfigurationElementsFor("com.dynamo.cr.manipulators");
        try {
            for (IConfigurationElement e : config) {
                if (e.getName().equals("manipulator-mode")) {
                    String id = e.getAttribute("id");
                    String name = e.getAttribute("name");
                    ManipulatorMode mode = new ManipulatorMode(id, name);
                    modes.put(id, mode);
                }
            }
            for (IConfigurationElement e : config) {
                if (e.getName().equals("manipulator")) {
                    String name = e.getAttribute("name");
                    String modeId = e.getAttribute("mode");
                    String nodeType = e.getAttribute("node-type");
                    ManipulatorMode mode = modes.get(modeId);
                    ManipulatorInfo info = new ManipulatorInfo(name, mode, nodeType);
                    mode.addManipulatorInfo(info);
                }
            }
        } catch (Exception exception) {
            Status status = new Status(IStatus.ERROR, Activator.PLUGIN_ID, exception.getMessage(), exception);
            plugin.getLog().log(status);
        }

    }

    @Override
    public IManipulatorMode getMode(String modeId) {
        return modes.get(modeId);
    }

    @Override
    public RootManipulator getManipulatorForSelection(IManipulatorMode mode, Object[] selection) {
        INodeTypeRegistry nodeTypeRegistry = Activator.getDefault().getNodeTypeRegistry();
        List<IManipulatorInfo> list = mode.getManipulatorInfoList();
        for (IManipulatorInfo info : list) {

            String nodeTypeID = info.getNodeType();
            INodeType nodeType = nodeTypeRegistry.getNodeTypeFromID(nodeTypeID);
            try {
                RootManipulator manipulator = (RootManipulator) nodeType.getNodeClass().newInstance();
                if (manipulator.match(selection)) {
                    return manipulator;
                }
            } catch (InstantiationException e) {
                Status status = new Status(IStatus.ERROR, Activator.PLUGIN_ID, e.getMessage(), e);
                plugin.getLog().log(status);
            } catch (IllegalAccessException e) {
                Status status = new Status(IStatus.ERROR, Activator.PLUGIN_ID, e.getMessage(), e);
                plugin.getLog().log(status);
            }
        }
        return null;
    }

}
