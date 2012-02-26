package com.dynamo.cr.go.core;

import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.eclipse.core.runtime.CoreException;
import org.eclipse.core.runtime.IProgressMonitor;
import org.eclipse.core.runtime.SubMonitor;

import com.dynamo.cr.sceneed.core.ILoaderContext;
import com.dynamo.cr.sceneed.core.INodeLoader;
import com.dynamo.cr.sceneed.core.Node;
import com.dynamo.cr.sceneed.core.util.LoaderUtil;
import com.dynamo.gameobject.proto.GameObject.CollectionDesc;
import com.dynamo.gameobject.proto.GameObject.CollectionDesc.Builder;
import com.dynamo.gameobject.proto.GameObject.CollectionInstanceDesc;
import com.dynamo.gameobject.proto.GameObject.InstanceDesc;
import com.google.protobuf.Message;
import com.google.protobuf.TextFormat;

public class CollectionLoader implements INodeLoader<CollectionNode> {

    @Override
    public CollectionNode load(ILoaderContext context, InputStream contents) throws IOException, CoreException {
        InputStreamReader reader = new InputStreamReader(contents);
        Builder builder = CollectionDesc.newBuilder();
        TextFormat.merge(reader, builder);
        CollectionDesc desc = builder.build();
        CollectionNode node = new CollectionNode();
        node.setName(desc.getName());
        Map<String, GameObjectInstanceNode> idToInstance = new HashMap<String, GameObjectInstanceNode>();
        Set<GameObjectInstanceNode> remainingInstances = new HashSet<GameObjectInstanceNode>();
        int n = desc.getInstancesCount();
        for (int i = 0; i < n; ++i) {
            InstanceDesc instanceDesc = desc.getInstances(i);
            String path = instanceDesc.getPrototype();
            GameObjectNode gameObjectNode = (GameObjectNode)context.loadNode(path);
            GameObjectInstanceNode instanceNode = new GameObjectInstanceNode(gameObjectNode);
            instanceNode.setTranslation(LoaderUtil.toPoint3d(instanceDesc.getPosition()));
            instanceNode.setRotation(LoaderUtil.toQuat4(instanceDesc.getRotation()));
            instanceNode.setId(instanceDesc.getId());
            instanceNode.setGameObject(path);
            idToInstance.put(instanceDesc.getId(), instanceNode);
            remainingInstances.add(instanceNode);
        }
        for (int i = 0; i < n; ++i) {
            InstanceDesc instanceDesc = desc.getInstances(i);
            Node parent = idToInstance.get(instanceDesc.getId());
            List<String> children = instanceDesc.getChildrenList();
            for (String childId : children) {
                Node child = idToInstance.get(childId);
                parent.addChild(child);
                remainingInstances.remove(child);
            }
        }
        for (GameObjectInstanceNode instance : remainingInstances) {
            node.addChild(instance);
        }
        n = desc.getCollectionInstancesCount();
        for (int i = 0; i < n; ++i) {
            CollectionInstanceDesc instanceDesc = desc.getCollectionInstances(i);
            String path = instanceDesc.getCollection();
            CollectionNode collectionNode = (CollectionNode)context.loadNode(path);
            CollectionInstanceNode instanceNode = new CollectionInstanceNode(collectionNode);
            instanceNode.setTranslation(LoaderUtil.toPoint3d(instanceDesc.getPosition()));
            instanceNode.setRotation(LoaderUtil.toQuat4(instanceDesc.getRotation()));
            instanceNode.setId(instanceDesc.getId());
            instanceNode.setCollection(path);
            node.addChild(instanceNode);
        }
        return node;
    }

    @Override
    public Message buildMessage(ILoaderContext context, CollectionNode collection, IProgressMonitor monitor)
            throws IOException, CoreException {
        Builder builder = CollectionDesc.newBuilder();
        builder.setName(collection.getName());
        buildInstances(collection, builder, monitor);
        return builder.build();
    }

    private void buildInstances(Node node, CollectionDesc.Builder builder, IProgressMonitor monitor) {
        SubMonitor progress = SubMonitor.convert(monitor, node.getChildren().size());
        for (Node child : node.getChildren()) {
            if (child instanceof GameObjectInstanceNode) {
                GameObjectInstanceNode instance = (GameObjectInstanceNode)child;
                InstanceDesc.Builder instanceBuilder = InstanceDesc.newBuilder();
                instanceBuilder.setPosition(LoaderUtil.toPoint3(instance.getTranslation()));
                instanceBuilder.setRotation(LoaderUtil.toQuat(instance.getRotation()));
                instanceBuilder.setId(instance.getId());
                instanceBuilder.setPrototype(instance.getGameObject());
                for (Node grandChild : child.getChildren()) {
                    if (grandChild instanceof GameObjectInstanceNode) {
                        instanceBuilder.addChildren(((GameObjectInstanceNode)grandChild).getId());
                    }
                }
                builder.addInstances(instanceBuilder);
                buildInstances(child, builder, monitor);
            } else if (child instanceof CollectionInstanceNode) {
                CollectionInstanceNode instance = (CollectionInstanceNode)child;
                CollectionInstanceDesc.Builder instanceBuilder = CollectionInstanceDesc.newBuilder();
                instanceBuilder.setPosition(LoaderUtil.toPoint3(instance.getTranslation()));
                instanceBuilder.setRotation(LoaderUtil.toQuat(instance.getRotation()));
                instanceBuilder.setId(instance.getId());
                instanceBuilder.setCollection(instance.getCollection());
                builder.addCollectionInstances(instanceBuilder);
            }
            progress.worked(1);
        }
    }
}
