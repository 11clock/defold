package com.dynamo.cr.integrationtest;

import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.not;
import static org.junit.Assert.assertThat;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.when;

import java.io.ByteArrayInputStream;
import java.io.CharArrayWriter;
import java.io.IOException;
import java.io.InputStream;

import org.eclipse.core.resources.IFile;
import org.eclipse.core.runtime.CoreException;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Path;
import org.junit.Test;

import com.dynamo.cr.go.core.ComponentTypeNode;
import com.dynamo.cr.go.core.GameObjectNode;
import com.dynamo.cr.go.core.RefComponentNode;
import com.dynamo.cr.go.core.operations.AddComponentOperation;
import com.dynamo.cr.tileeditor.scene.SpriteNode;
import com.dynamo.sprite.proto.Sprite.SpriteDesc;
import com.google.protobuf.TextFormat;

public class GameObjectReloadTest extends AbstractSceneTest {

    @Override
    public void setup() throws CoreException, IOException {
        super.setup();

        getPresenter().onLoad("go", new ByteArrayInputStream("".getBytes()));
    }

    private void saveSpriteComponent(String path, String tileSet, String defaultAnimation) throws IOException, CoreException {
        SpriteDesc.Builder builder = SpriteDesc.newBuilder();
        builder.setTileSet(tileSet).setDefaultAnimation(defaultAnimation);
        IFile file = getContentRoot().getFile(new Path(path));
        CharArrayWriter output = new CharArrayWriter();
        TextFormat.print(builder.build(), output);
        output.close();
        InputStream stream = new ByteArrayInputStream(output.toString().getBytes());
        if (!file.exists()) {
            file.create(stream, true, null);
        } else {
            file.setContents(stream, 0, null);
        }
    }

    // Tests

    @Test
    public void testReloadComponentFromFile() throws Exception {
        String path = "/sprite/reload.sprite";
        String tileSet = "/tileset/test.tileset";
        String defaultAnimation = "test";

        when(getPresenterContext().selectFile(anyString(), any(String[].class))).thenReturn(path);
        SpriteNode componentType = new SpriteNode();
        componentType.setTileSet(tileSet);
        componentType.setDefaultAnimation(defaultAnimation);

        saveSpriteComponent(path, "", "");

        GameObjectNode go = (GameObjectNode)getModel().getRoot();
        RefComponentNode component = new RefComponentNode(new SpriteNode());
        component.setComponent(path);
        AddComponentOperation op = new AddComponentOperation(go, component, getPresenterContext());
        getModel().executeOperation(op);
        assertThat(go.getChildren().size(), is(1));
        assertNodePropertyStatus(component, "component", IStatus.ERROR, null);
        ComponentTypeNode type = component.getType();

        saveSpriteComponent(path, tileSet, defaultAnimation);

        assertNodePropertyStatus(component, "component", IStatus.OK, null);
        assertThat((RefComponentNode)go.getChildren().get(0), is(component));
        assertThat(type, is(not(component.getType())));
    }

}
