package com.dynamo.cr.go.core.test;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.junit.Assert.assertThat;

import java.io.IOException;
import java.util.Collections;
import java.util.List;

import org.eclipse.core.runtime.CoreException;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.osgi.util.NLS;
import org.hamcrest.core.IsInstanceOf;
import org.junit.Before;
import org.junit.Test;

import com.dynamo.cr.go.core.CollectionInstanceNode;
import com.dynamo.cr.go.core.CollectionLoader;
import com.dynamo.cr.go.core.CollectionNode;
import com.dynamo.cr.go.core.GameObjectInstanceNode;
import com.dynamo.cr.go.core.GameObjectLoader;
import com.dynamo.cr.go.core.GameObjectNode;
import com.dynamo.cr.go.core.InstanceNode;
import com.dynamo.cr.go.core.Messages;
import com.dynamo.cr.go.core.operations.AddInstanceOperation;
import com.dynamo.cr.sceneed.core.Node;
import com.dynamo.cr.sceneed.core.operations.RemoveChildrenOperation;
import com.dynamo.cr.sceneed.core.test.AbstractNodeTest;
import com.dynamo.gameobject.proto.GameObject.CollectionDesc;

public class CollectionNodeTest extends AbstractNodeTest {

    private CollectionLoader loader;
    private CollectionNode collectionNode;
    private GameObjectLoader gameObjectLoader;

    @Override
    @Before
    public void setup() throws CoreException, IOException {
        super.setup();

        this.loader = new CollectionLoader();
        this.gameObjectLoader = new GameObjectLoader();

        // Virtual files
        registerFile("/test.go", "");
        registerFile("/test.collection", "name: \"default\"");

        this.collectionNode = registerAndLoadNodeType(CollectionNode.class, "collection", this.loader);
    }

    // Helpers

    private void addInstance(InstanceNode instance) throws Exception {
        execute(new AddInstanceOperation(this.collectionNode, instance, getPresenterContext()));
        verifySelection();
    }

    private void addGameObject() throws Exception {
        GameObjectNode gameObject = this.gameObjectLoader.load(getLoaderContext(), getFile("/test.go").getContents());
        GameObjectInstanceNode instance = new GameObjectInstanceNode(gameObject);
        instance.setGameObject("/test.go");
        addInstance(instance);
    }

    private void addCollection() throws Exception {
        CollectionNode collection = this.loader.load(getLoaderContext(), getFile("/test.collection").getContents());
        CollectionInstanceNode instance = new CollectionInstanceNode(collection);
        instance.setCollection("/test.collection");
        addInstance(instance);
    }

    private void removeInstance(int i) throws Exception {
        execute(new RemoveChildrenOperation(Collections.singletonList((Node)instance(i)), getPresenterContext()));
        verifySelection();
    }

    private void removeGameObject(int i) throws Exception {
        assertThat(instance(i), IsInstanceOf.instanceOf(GameObjectInstanceNode.class));
        removeInstance(i);
    }

    private void removeCollection(int i) throws Exception {
        assertThat(instance(i), IsInstanceOf.instanceOf(CollectionInstanceNode.class));
        removeInstance(i);
    }

    private int instanceCount() {
        return this.collectionNode.getChildren().size();
    }

    private InstanceNode instance(int i) {
        return (InstanceNode)this.collectionNode.getChildren().get(i);
    }

    private void assertOneInstance() {
        assertThat(instanceCount(), is(1));
        assertThat(instance(0).getId(), is("test"));
        assertThat((CollectionNode)instance(0).getParent(), equalTo(this.collectionNode));
    }

    private void assertNoComponent() {
        assertThat(instanceCount(), is(0));
    }

    // Tests

    @Test
    public void testAddGameObject() throws Exception {
        addGameObject();

        assertOneInstance();

        undo();
        assertNoComponent();
        verifySelection();

        redo();
        assertOneInstance();
        verifySelection();
    }

    @Test
    public void testAddCollection() throws Exception {
        addCollection();

        assertOneInstance();

        undo();
        assertNoComponent();
        verifySelection();

        redo();
        assertOneInstance();
        verifySelection();
    }

    @Test
    public void testRemoveGameObject() throws Exception {
        addGameObject();

        InstanceNode instance = instance(0);

        removeGameObject(0);
        assertNoComponent();
        assertThat(instance.getParent(), equalTo(null));

        undo();
        assertOneInstance();
        verifySelection();

        redo();
        assertNoComponent();
        assertThat(instance.getParent(), equalTo(null));
        verifySelection();
    }

    @Test
    public void testRemoveCollection() throws Exception {
        addCollection();

        InstanceNode instance = instance(0);

        removeCollection(0);
        assertNoComponent();
        assertThat(instance.getParent(), equalTo(null));

        undo();
        assertOneInstance();
        verifySelection();

        redo();
        assertNoComponent();
        assertThat(instance.getParent(), equalTo(null));
        verifySelection();
    }

    @Test
    public void testSetId() throws Exception {
        addGameObject();

        InstanceNode instance = instance(0);

        String oldId = instance.getId();
        String newId = "test1";

        setNodeProperty(instance, "id", newId);
        assertThat(instance.getId(), is(newId));

        undo();
        assertThat(instance.getId(), is(oldId));

        redo();
        assertThat(instance.getId(), is(newId));
    }

    @Test
    public void testBuildMessage() throws Exception {
        addCollection();

        addGameObject();

        CollectionDesc ddf = (CollectionDesc)this.loader.buildMessage(getLoaderContext(), this.collectionNode, null);

        assertThat(ddf.getCollectionInstancesCount(), is(1));
        assertThat(ddf.getInstancesCount(), is(1));
        assertThat(ddf.getCollectionInstances(0).getId(), is(instance(0).getId()));
        assertThat(ddf.getInstances(0).getId(), is(instance(1).getId()));
    }

    @Test
    public void testUniqueId() throws Exception {
        addGameObject();
        assertThat(instance(0).getId(), is("test"));

        addGameObject();
        assertThat(instance(1).getId(), is("test1"));

        addCollection();
        assertThat(instance(0).getId(), is("test2"));
        assertThat(instance(1).getId(), is("test"));
    }

    @Test
    public void testChild() throws Exception {
        registerFile("/child.collection", "name: \"default\" "
                + "instances {id: \"node1\" prototype: \"/test.go\" children: \"node2\"} "
                + "instances {id: \"node2\" prototype: \"/test.go\"}");

        CollectionNode collection = this.loader.load(getLoaderContext(), getFile("/child.collection").getContents());
        List<Node> gos = collection.getChildren();
        assertThat(gos.size(), is(1));
        List<Node> children = gos.get(0).getChildren();
        assertThat(children.size(), is(1));
    }

    @Test
    public void testInstanceMessages() throws Exception {
        addGameObject();

        InstanceNode instance = instance(0);

        assertNodePropertyStatus(instance, "id", IStatus.OK, null);

        setNodeProperty(instance, "id", "");
        assertNodePropertyStatus(instance, "id", IStatus.ERROR, Messages.InstanceNode_id_EMPTY);

        setNodeProperty(instance, "id", "test");
        assertNodePropertyStatus(instance, "id", IStatus.OK, null);

        addGameObject();

        setNodeProperty(instance(1), "id", "test");
        assertNodePropertyStatus(instance(1), "id", IStatus.ERROR, NLS.bind(Messages.InstanceNode_id_DUPLICATE, "test"));
    }

    @Test
    public void testGameObjectMessages() throws Exception {

        addGameObject();

        GameObjectNode gameObject = new GameObjectNode();
        gameObject.addChild(new DummyComponentNode());
        registerLoadedNode("/invalid.go", gameObject);

        GameObjectInstanceNode instance = (GameObjectInstanceNode)instance(0);
        instance.setGameObject("/invalid.go");

        assertNodePropertyStatus(instance, "gameObject", IStatus.ERROR, Messages.GameObjectInstanceNode_gameObject_INVALID_REFERENCE);

        setNodeProperty(instance, "gameObject", "/test.test2");
        assertNodePropertyStatus(instance, "gameObject", IStatus.ERROR, NLS.bind(Messages.GameObjectInstanceNode_gameObject_INVALID_TYPE, "test2"));

        setNodeProperty(instance, "gameObject", "/test");
        assertNodePropertyStatus(instance, "gameObject", IStatus.ERROR, NLS.bind(Messages.GameObjectInstanceNode_gameObject_UNKNOWN_TYPE, "/test"));
    }

    @Test
    public void testCollectionMessages() throws Exception {

        addCollection();

        registerLoadedNode("/invalid.collection", new CollectionNode());
        CollectionInstanceNode instance = (CollectionInstanceNode)instance(0);
        instance.setCollection("/invalid.collection");

        assertNodePropertyStatus(instance, "collection", IStatus.ERROR, Messages.CollectionInstanceNode_collection_INVALID_REFERENCE);

        setNodeProperty(instance, "collection", "/test.test2");
        assertNodePropertyStatus(instance, "collection", IStatus.ERROR, NLS.bind(Messages.CollectionInstanceNode_collection_INVALID_TYPE, "test2"));

        setNodeProperty(instance, "collection", "/test");
        assertNodePropertyStatus(instance, "collection", IStatus.ERROR, NLS.bind(Messages.CollectionInstanceNode_collection_UNKNOWN_TYPE, "/test"));
    }

}
