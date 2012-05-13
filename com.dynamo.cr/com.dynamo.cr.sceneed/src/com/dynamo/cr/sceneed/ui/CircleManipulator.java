package com.dynamo.cr.sceneed.ui;

import javax.vecmath.AxisAngle4d;
import javax.vecmath.Matrix4d;
import javax.vecmath.Quat4d;
import javax.vecmath.Vector4d;

import org.eclipse.swt.events.MouseEvent;

import com.dynamo.cr.sceneed.core.Manipulator;

@SuppressWarnings("serial")
public class CircleManipulator extends Manipulator {

    private RootManipulator rootManipulator;
    private float[] color;
    private boolean rotating = false;
    private int startX;
    private double angle;
    private Quat4d originalRotation = new Quat4d();

    public CircleManipulator(RootManipulator rootManipulator, float[] color) {
        this.rootManipulator = rootManipulator;
        this.color = color;
    }

    @Override
    public boolean match(Object[] selection) {
        return false;
    }

    public float[] getColor() {
        return color;
    }

    @Override
    public void mouseDown(MouseEvent e) {
        if (getController().isManipulatorSelected(this)) {
            startX = e.x;
            rotating = true;
            originalRotation = rootManipulator.getRotation();
        }
    }

    @Override
    public void mouseUp(MouseEvent e) {
        rotating = false;
    }

    @Override
    public void mouseMove(MouseEvent e) {
        if (rotating) {
            int dx = e.x - startX;
            double mouseAngle = dx * 0.02;
            this.angle = mouseAngle;

            Matrix4d transform = new Matrix4d();
            getLocalTransform(transform);
            Vector4d axis = new Vector4d(1, 0, 0, 0);
            transform.transform(axis);

            AxisAngle4d aa = new AxisAngle4d(axis.getX(), axis.getY(), axis.getZ(), this.angle);
            Quat4d delta = new Quat4d();
            delta.set(aa);
            // Combine original rotation with local manipulator rotation
            Quat4d q = new Quat4d(originalRotation);
            q.mul(delta);
            rootManipulator.setRotation(q);
            rootManipulator.transformChanged();
        }
    }

}
