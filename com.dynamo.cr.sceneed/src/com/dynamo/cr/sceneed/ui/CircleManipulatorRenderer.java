package com.dynamo.cr.sceneed.ui;

import javax.media.opengl.GL;
import javax.vecmath.Vector3d;

import com.dynamo.cr.sceneed.core.INodeRenderer;
import com.dynamo.cr.sceneed.core.RenderContext;
import com.dynamo.cr.sceneed.core.RenderContext.Pass;
import com.dynamo.cr.sceneed.core.RenderData;

public class CircleManipulatorRenderer implements INodeRenderer<CircleManipulator> {

    public CircleManipulatorRenderer() {
    }

    @Override
    public void setup(RenderContext renderContext, CircleManipulator node) {
        Pass pass = renderContext.getPass();
        if (pass == Pass.MANIPULATOR || pass == Pass.SELECTION) {
            renderContext.add(this, node, new Vector3d(), null);
        }
    }

    @Override
    public void render(RenderContext renderContext, CircleManipulator node,
            RenderData<CircleManipulator> renderData) {
        float[] color = node.getColor();
        GL gl = renderContext.getGL();

        double factor = ManipulatorRendererUtil.getScaleFactor(node, renderContext.getRenderView());
        double radius = ManipulatorRendererUtil.BASE_LENGTH  / factor;
        gl.glColor4fv(color, 0);
        RenderUtil.drawCircle(gl, radius);
    }

}
