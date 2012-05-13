package com.dynamo.cr.integrationtest;

import static org.junit.Assert.assertEquals;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

import org.eclipse.core.resources.IFile;
import org.eclipse.core.runtime.CoreException;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Path;
import org.junit.Test;

import com.dynamo.cr.go.core.CollectionNode;
import com.dynamo.cr.go.core.ComponentPropertyNode;
import com.dynamo.cr.go.core.GameObjectInstanceNode;

public class ScriptPropertiesCollectionTest extends AbstractSceneTest {

    @Override
    public void setup() throws CoreException, IOException {
        super.setup();

        getPresenter().onLoad("collection", new ByteArrayInputStream("name: \"main\" instances { id: \"go\" prototype: \"/game_object/props.go\"}".getBytes()));
    }

    private void saveScript(String path, String content) throws IOException, CoreException {
        IFile file = getContentRoot().getFile(new Path(path));
        InputStream stream = new ByteArrayInputStream(content.getBytes());
        if (!file.exists()) {
            file.create(stream, true, null);
        } else {
            file.setContents(stream, 0, null);
        }
    }

    // Tests

    @Test
    public void testAccess() throws Exception {

        CollectionNode collection = (CollectionNode)getModel().getRoot();
        GameObjectInstanceNode gameObject = (GameObjectInstanceNode)collection.getChildren().get(0);
        ComponentPropertyNode component = (ComponentPropertyNode)gameObject.getChildren().get(1);

        // Default value
        assertEquals("2", getNodeProperty(component, "number"));
        assertEquals("hash2", getNodeProperty(component, "hash"));
        assertEquals("/url", getNodeProperty(component, "url"));

        // Set value
        setNodeProperty(component, "number", "3");
        assertEquals("3", getNodeProperty(component, "number"));
        setNodeProperty(component, "hash", "hash3");
        assertEquals("hash3", getNodeProperty(component, "hash"));
        setNodeProperty(component, "url", "/url2");
        assertEquals("/url2", getNodeProperty(component, "url"));

        // Reset to default
        setNodeProperty(component, "number", "");
        assertEquals("2", getNodeProperty(component, "number"));
        setNodeProperty(component, "hash", "");
        assertEquals("hash2", getNodeProperty(component, "hash"));
        setNodeProperty(component, "url", "");
        assertEquals("/url", getNodeProperty(component, "url"));

        // Validation
        assertNodePropertyStatus(component, "number", IStatus.OK, null);
        setNodeProperty(component, "number", "invalid");
        assertNodePropertyStatus(component, "number", IStatus.ERROR, null);

        assertNodePropertyStatus(component, "url", IStatus.OK, null);
        setNodeProperty(component, "url", "invalid");
        assertNodePropertyStatus(component, "url", IStatus.ERROR, null);
    }

    @Test
    public void testLoad() throws Exception {
        getPresenter().onLoad("collection", ((IFile)getContentRoot().findMember("/collection/props.collection")).getContents());

        CollectionNode collection = (CollectionNode)getModel().getRoot();
        GameObjectInstanceNode gameObject = (GameObjectInstanceNode)collection.getChildren().get(0);
        ComponentPropertyNode component = (ComponentPropertyNode)gameObject.getChildren().get(1);

        assertEquals("3", getNodeProperty(component, "number"));
        assertEquals("hash3", getNodeProperty(component, "hash"));
        assertEquals("/url2", getNodeProperty(component, "url"));
    }

    @Test
    public void testSave() throws Exception {
        IFile collectionFile = (IFile)getContentRoot().findMember("/collection/props.collection");
        getPresenter().onLoad("collection", collectionFile.getContents());

        CollectionNode collection = (CollectionNode)getModel().getRoot();
        GameObjectInstanceNode gameObject = (GameObjectInstanceNode)collection.getChildren().get(0);
        ComponentPropertyNode component = (ComponentPropertyNode)gameObject.getChildren().get(1);

        setNodeProperty(component, "number", "4");
        setNodeProperty(component, "hash", "hash4");
        setNodeProperty(component, "url", "/url3");

        ByteArrayOutputStream stream = new ByteArrayOutputStream();
        getPresenter().onSave(stream, null);
        collectionFile.setContents(new ByteArrayInputStream(stream.toByteArray()), false, true, null);

        getPresenter().onLoad("collection", collectionFile.getContents());
        collection = (CollectionNode)getModel().getRoot();
        gameObject = (GameObjectInstanceNode)collection.getChildren().get(0);
        component = (ComponentPropertyNode)gameObject.getChildren().get(1);

        assertEquals("4", getNodeProperty(component, "number"));
        assertEquals("hash4", getNodeProperty(component, "hash"));
        assertEquals("/url3", getNodeProperty(component, "url"));
    }

    @Test(expected = RuntimeException.class)
    public void testReload() throws Exception {
        CollectionNode collection = (CollectionNode)getModel().getRoot();
        GameObjectInstanceNode gameObject = (GameObjectInstanceNode)collection.getChildren().get(0);
        ComponentPropertyNode component = (ComponentPropertyNode)gameObject.getChildren().get(1);

        // Default value
        assertEquals("2", getNodeProperty(component, "number"));

        saveScript("/script/props.script", "go.property(\"number2\", 0)");

        getNodeProperty(component, "number");
    }

    @Test
    public void testSavePropEqualToDefault() throws Exception {
        getPresenter().onLoad("collection", ((IFile)getContentRoot().findMember("/collection/empty_props_go.collection")).getContents());

        CollectionNode collection = (CollectionNode)getModel().getRoot();
        GameObjectInstanceNode gameObject = (GameObjectInstanceNode)collection.getChildren().get(0);
        ComponentPropertyNode component = (ComponentPropertyNode)gameObject.getChildren().get(1);

        assertEquals("3", getNodeProperty(component, "number"));

        // Set to default
        setNodeProperty(component, "number", "1");

        // Save file
        ByteArrayOutputStream stream = new ByteArrayOutputStream();
        getPresenter().onSave(stream, null);
        IFile collectionFile = (IFile)getContentRoot().findMember("/collection/empty_props_go.collection");
        collectionFile.setContents(new ByteArrayInputStream(stream.toByteArray()), false, true, null);

        // Change script to new value
        saveScript("/script/props.script", "go.property(\"number\", 3)");

        // Load file again
        getPresenter().onLoad("collection", ((IFile)getContentRoot().findMember("/collection/empty_props_go.collection")).getContents());

        collection = (CollectionNode)getModel().getRoot();
        gameObject = (GameObjectInstanceNode)collection.getChildren().get(0);
        component = (ComponentPropertyNode)gameObject.getChildren().get(1);

        // Verify new value
        assertEquals("1", getNodeProperty(component, "number"));
    }
}
