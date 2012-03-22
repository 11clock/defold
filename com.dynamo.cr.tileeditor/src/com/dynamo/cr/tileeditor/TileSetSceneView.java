package com.dynamo.cr.tileeditor;

import javax.inject.Inject;
import javax.vecmath.Point3d;

import org.eclipse.jface.viewers.ILabelProvider;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.swt.widgets.Display;

import com.dynamo.cr.properties.IFormPropertySheetPage;
import com.dynamo.cr.sceneed.core.IImageProvider;
import com.dynamo.cr.sceneed.core.ISceneView;
import com.dynamo.cr.sceneed.core.ManipulatorController;
import com.dynamo.cr.sceneed.core.Node;
import com.dynamo.cr.sceneed.ui.ISceneOutlinePage;
import com.dynamo.cr.tileeditor.scene.TileSetNode;

public class TileSetSceneView implements ISceneView {

    @Inject private ISceneOutlinePage outline;
    @Inject private IFormPropertySheetPage propertySheetPage;
    @Inject
    private TileSetRenderer2 renderer;
    @Inject
    private TileSetEditor2 editor;
    @Inject
    IImageProvider imageProvider;
    @Inject ManipulatorController manipulatorController;

    @Override
    public void setRoot(Node root) {
        TileSetNode tileSet = (TileSetNode) root;
        this.outline.setInput(root);
        this.renderer.setInput(tileSet);
    }

    @Override
    public void refresh(IStructuredSelection selection, boolean dirty) {
        this.outline.refresh();
        this.outline.setSelection(selection);
        this.propertySheetPage.setSelection(selection);
        this.propertySheetPage.refresh();
        this.manipulatorController.setSelection(selection);
        this.renderer.refresh(selection);
        this.editor.setDirty(dirty);
    }

    @Override
    public void refreshRenderView() {
        this.renderer.requestPaint();
    }

    @Override
    public void asyncExec(Runnable runnable) {
        Display.getCurrent().asyncExec(runnable);
    }

    @Override
    public String selectFromList(String title, String message, String... lst) {
        throw new RuntimeException("Not supported");
    }

    @Override
    public Object selectFromArray(String title, String message, Object[] input, ILabelProvider labelProvider) {
        // TODO Auto-generated method stub
        return null;
    }

    @Override
    public String selectFile(String title, String[] extensions) {
        // TODO Auto-generated method stub
        return null;
    }

    @Override
    public void getCameraFocusPoint(Point3d focusPoint) {
        throw new RuntimeException("Not implemented");
    }

}
