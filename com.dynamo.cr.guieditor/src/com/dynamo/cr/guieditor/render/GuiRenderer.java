package com.dynamo.cr.guieditor.render;

import java.awt.Font;
import java.awt.FontMetrics;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.geom.Rectangle2D;
import java.awt.image.BufferedImage;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.IntBuffer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import javax.media.opengl.GL;
import javax.media.opengl.glu.GLU;

import org.eclipse.ui.services.IDisposable;

import com.dynamo.gui.proto.Gui.NodeDesc.BlendMode;
import com.sun.opengl.util.j2d.TextRenderer;
import com.sun.opengl.util.texture.Texture;

public class GuiRenderer implements IDisposable, IGuiRenderer {
    private ArrayList<RenderCommmand> renderCommands = new ArrayList<GuiRenderer.RenderCommmand>();
    private GL gl;
    private int currentName;

    // Picking
    private static final int MAX_NODES = 4096;
    private static IntBuffer selectBuffer = ByteBuffer.allocateDirect(4 * MAX_NODES).order(ByteOrder.nativeOrder()).asIntBuffer();
    private TextRenderer debugTextRenderer;
    private Graphics2D graphics;

    public GuiRenderer() {
        BufferedImage image = new BufferedImage(4, 4, BufferedImage.TYPE_INT_ARGB);
        graphics = (Graphics2D) image.getGraphics();
        graphics.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
        graphics.setRenderingHint(RenderingHints.KEY_FRACTIONALMETRICS, RenderingHints.VALUE_FRACTIONALMETRICS_ON);
    }

    private abstract class RenderCommmand {

        private BlendMode blendMode;
        private Texture texture;
        public RenderCommmand(double r, double g, double b, double a, BlendMode blendMode, Texture texture) {
            this.r = r;
            this.g = g;
            this.b = b;
            this.a = a;
            this.blendMode = blendMode;
            this.texture = texture;
        }
        double r, g, b, a;
        int name = -1;

        public void setupBlendMode() {
            if (blendMode != null) {
                gl.glEnable(GL.GL_BLEND);
                switch (blendMode) {

                case BLEND_MODE_ALPHA:
                    gl.glBlendFunc (GL.GL_SRC_ALPHA, GL.GL_ONE_MINUS_SRC_ALPHA);
                break;

                case BLEND_MODE_ADD:
                    gl.glBlendFunc(GL.GL_ONE, GL.GL_ONE);
                break;

                case BLEND_MODE_ADD_ALPHA:
                    gl.glBlendFunc(GL.GL_ONE, GL.GL_SRC_ALPHA);
                    break;

                case BLEND_MODE_MULT:
                    gl.glBlendFunc(GL.GL_ZERO, GL.GL_SRC_COLOR);
                    break;
                }
            }
            else {
                gl.glDisable(GL.GL_BLEND);
            }
        }

        public void setupTexture() {
            if (texture != null) {
                gl.glEnable(GL.GL_TEXTURE_2D);
                gl.glTexEnvf(GL.GL_TEXTURE_ENV, GL.GL_TEXTURE_ENV_MODE, GL.GL_MODULATE);
                texture.bind();
            }
            else {
                gl.glDisable(GL.GL_TEXTURE_2D);
            }
        }

        public abstract void draw(GL gl);
    }

    private class TextRenderCommmand extends RenderCommmand {
        private String text;
        private double x0, y0;
        private TextRenderer textRenderer;

        public TextRenderCommmand(TextRenderer textRenderer, String text, double x0, double y0, double r, double g, double b, double a, BlendMode blendMode, Texture texture) {
            super(r, g, b, a, blendMode, texture);
            this.textRenderer = textRenderer;
            this.text = text;
            this.x0 = x0;
            this.y0 = y0;
        }

        @Override
        public void draw(GL gl) {
            textRenderer.begin3DRendering();
            gl.glColor4d(r, g, b, a);
            setupBlendMode();
            textRenderer.draw3D(text, (float) x0, (float) y0, 0, 1);
            textRenderer.end3DRendering();
        }
    }

    private class QuadRenderCommand extends RenderCommmand {
        private double x0, y0, x1, y1;

        public QuadRenderCommand(double x0, double y0, double x1, double y1, double r, double g, double b, double a, BlendMode blendMode, Texture texture) {
            super(r, g, b, a, blendMode, texture);
            this.x0 = x0;
            this.y0 = y0;
            this.x1 = x1;
            this.y1 = y1;
        }

        @Override
        public void draw(GL gl) {
            if (name != -1)
                gl.glPushName(name);

            setupBlendMode();
            setupTexture();

            gl.glBegin(GL.GL_QUADS);
            gl.glColor4d(r, g, b, a);

            gl.glTexCoord2d(0, 1);
            gl.glVertex2d(x0, y0);

            gl.glTexCoord2d(1, 1);
            gl.glVertex2d(x1, y0);

            gl.glTexCoord2d(1, 0);
            gl.glVertex2d(x1, y1);

            gl.glTexCoord2d(0, 0);
            gl.glVertex2d(x0, y1);
            gl.glEnd();

            if (name != -1)
                gl.glPopName();

        }
    }

    @Override
    public void dispose() {
    }

    /* (non-Javadoc)
     * @see com.dynamo.cr.guieditor.render.IGuiRenderer#begin(javax.media.opengl.GL)
     */
    @Override
    public void begin(GL gl) {
        this.renderCommands.clear();
        this.renderCommands.ensureCapacity(1024);
        this.gl = gl;
        this.currentName = -1;

        if (debugTextRenderer == null) {
            String fontName = Font.SANS_SERIF;
            Font debugFont = new Font(fontName, Font.BOLD, 28);
            debugTextRenderer = new TextRenderer(debugFont, true, true);
        }
    }

    /* (non-Javadoc)
     * @see com.dynamo.cr.guieditor.render.IGuiRenderer#end()
     */
    @Override
    public void end() {
        for (RenderCommmand command : renderCommands) {
            command.draw(gl);
        }
    }

    /* (non-Javadoc)
     * @see com.dynamo.cr.guieditor.render.IGuiRenderer#drawQuad(double, double, double, double, double, double, double, double, com.dynamo.gui.proto.Gui.NodeDesc.BlendMode, java.lang.String)
     */
    @Override
    public void drawQuad(double x0, double y0, double x1, double y1, double r, double g, double b, double a, BlendMode blendMode, Texture texture) {
        QuadRenderCommand command = new QuadRenderCommand(x0, y0, x1, y1, r, g, b, a, blendMode, texture);
        if (currentName != -1) {
            command.name = currentName;
        }
        this.renderCommands.add(command);
    }

    /* (non-Javadoc)
     * @see com.dynamo.cr.guieditor.render.IGuiRenderer#drawString(java.lang.String, java.lang.String, double, double, double, double, double, double, com.dynamo.gui.proto.Gui.NodeDesc.BlendMode, java.lang.String)
     */
    @Override
    public void drawString(TextRenderer textRenderer, String text, double x0, double y0, double r, double g, double b, double a, BlendMode blendMode, Texture texture) {
        if (textRenderer == null)
            textRenderer = debugTextRenderer;

        TextRenderCommmand command = new TextRenderCommmand(textRenderer, text, x0, y0, r, g, b, a, blendMode, texture);
        if (currentName != -1) {
            command.name = currentName;
        }
        this.renderCommands.add(command);
    }

    /* (non-Javadoc)
     * @see com.dynamo.cr.guieditor.render.IGuiRenderer#drawStringBounds(java.lang.String, java.lang.String, double, double, double, double, double, double)
     */
    @Override
    public void drawStringBounds(TextRenderer textRenderer, String text, double x0, double y0, double r, double g, double b, double a) {
        if (textRenderer == null)
            textRenderer = debugTextRenderer;

        Rectangle2D bounds = textRenderer.getBounds(text);
        double x = x0 + bounds.getX();
        double y = y0 - (bounds.getHeight() + bounds.getY());
        double w = bounds.getWidth();
        double h = bounds.getHeight();
        QuadRenderCommand command = new QuadRenderCommand(x, y, x + w, y + h, r, g, b, a, null, null);
        if (currentName != -1) {
            command.name = currentName;
        }
        this.renderCommands.add(command);
    }

    /* (non-Javadoc)
     * @see com.dynamo.cr.guieditor.render.IGuiRenderer#getStringBounds(java.lang.String, java.lang.String)
     */
    @Override
    public Rectangle2D getStringBounds(TextRenderer textRenderer, String text) {
        if (textRenderer == null)
            textRenderer = debugTextRenderer;

        return textRenderer.getBounds(text);
    }

    /* (non-Javadoc)
     * @see com.dynamo.cr.guieditor.render.IGuiRenderer#setName(int)
     */
    @Override
    public void setName(int name) {
        this.currentName = name;
    }

    /* (non-Javadoc)
     * @see com.dynamo.cr.guieditor.render.IGuiRenderer#clearName()
     */
    @Override
    public void clearName() {
        this.currentName = -1;
    }

    /* (non-Javadoc)
     * @see com.dynamo.cr.guieditor.render.IGuiRenderer#beginSelect(javax.media.opengl.GL, int, int, int, int, int[])
     */
    @Override
    public void beginSelect(GL gl, int x, int y, int w, int h, int viewPort[]) {
        begin(gl);
        gl.glSelectBuffer(MAX_NODES, selectBuffer);
        gl.glRenderMode(GL.GL_SELECT);

        GLU glu = new GLU();

        gl.glMatrixMode(GL.GL_PROJECTION);
        gl.glLoadIdentity();
        glu.gluPickMatrix(x, y, w, h, viewPort, 0);
        glu.gluOrtho2D(0, viewPort[2], 0, viewPort[3]);

        gl.glMatrixMode(GL.GL_MODELVIEW);
        gl.glLoadIdentity();

        gl.glInitNames();
    }

    private static long toUnsignedInt(int i)
    {
        long tmp = i;
        return ( tmp << 32 ) >>> 32;
    }

    /* (non-Javadoc)
     * @see com.dynamo.cr.guieditor.render.IGuiRenderer#endSelect()
     */
    @Override
    public SelectResult endSelect()
    {
        end();

        long minz;
        minz = Long.MAX_VALUE;

        gl.glFlush();
        int hits = gl.glRenderMode(GL.GL_RENDER);

        List<SelectResult.Pair> selected = new ArrayList<SelectResult.Pair>();

        int names, ptr, ptrNames = 0, numberOfNames = 0;
        ptr = 0;
        for (int i = 0; i < hits; i++)
        {
           names = selectBuffer.get(ptr);
           ptr++;
           {
               numberOfNames = names;
               minz = toUnsignedInt(selectBuffer.get(ptr));
               ptrNames = ptr+2;
               selected.add(new SelectResult.Pair(minz, selectBuffer.get(ptrNames)));
           }

           ptr += names+2;
        }
        ptr = ptrNames;

        Collections.sort(selected);

        if (numberOfNames > 0)
        {
            return new SelectResult(selected, minz);
        }
        else
        {
            return new SelectResult(selected, minz);
        }
    }

    @Override
    public FontMetrics getFontMetrics(Font font) {
        return graphics.getFontMetrics(font);
    }

    @Override
    public TextRenderer getDebugTextRenderer() {
        return debugTextRenderer;
    }
}
