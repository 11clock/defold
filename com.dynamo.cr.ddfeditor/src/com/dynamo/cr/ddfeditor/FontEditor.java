package com.dynamo.cr.ddfeditor;

import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.LinkedBlockingQueue;

import org.apache.commons.io.IOUtils;
import org.eclipse.core.resources.IContainer;
import org.eclipse.core.resources.IFile;
import org.eclipse.core.runtime.IStatus;
import org.eclipse.core.runtime.Path;
import org.eclipse.core.runtime.Status;
import org.eclipse.swt.SWT;
import org.eclipse.swt.custom.ScrolledComposite;
import org.eclipse.swt.events.PaintEvent;
import org.eclipse.swt.events.PaintListener;
import org.eclipse.swt.graphics.Image;
import org.eclipse.swt.graphics.ImageData;
import org.eclipse.swt.graphics.PaletteData;
import org.eclipse.swt.layout.FillLayout;
import org.eclipse.swt.widgets.Canvas;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.statushandlers.StatusManager;

import com.dynamo.cr.protobind.MessageNode;
import com.dynamo.render.Fontc;
import com.dynamo.render.proto.Font.FontDesc;

public class FontEditor extends DdfEditor {

    private ScrolledComposite scrolledComposite;
    private Canvas canvas;
    private Image image;
    private IContainer root;
    private PreviewThread previewThread;
    private boolean previewErrorLogged;

    class PreviewThread extends Thread {
        boolean run = true;
        LinkedBlockingQueue<FontDesc> queue = new LinkedBlockingQueue<FontDesc>();
        String cachedFontName = "";
        private byte[] cachedFont;

        @Override
        public void run() {
            while (this.run) {
                try {
                    FontDesc fontDesc = queue.take();
                    while (queue.size() > 0) {
                        fontDesc = queue.take();
                    }

                    if (!fontDesc.getFont().equals("")) {
                        if (!cachedFontName.equals(fontDesc.getFont())) {
                            IFile fontFile = root.getFile(new Path(fontDesc.getFont()));
                            InputStream fontStream = fontFile.getContents();
                            try {
                                ByteArrayOutputStream fontOut = new ByteArrayOutputStream();
                                IOUtils.copy(fontStream, fontOut);
                                cachedFont = fontOut.toByteArray();
                            }
                            finally {
                                fontStream.close();
                            }
                        }

                        final BufferedImage awtImage = Fontc.compileToImage(new ByteArrayInputStream(cachedFont), fontDesc);
                        final Display display = Display.getDefault();

                        Display.getDefault().asyncExec(new Runnable() {

                            @Override
                            public void run() {
                                if (PreviewThread.this.run) {
                                    image = new Image(display, convertToSWT(awtImage));
                                    canvas.setSize(image.getBounds().width, image.getBounds().height);
                                    canvas.redraw();
                                }
                            }
                        });
                    }

                } catch (InterruptedException e) {

                } catch (final Exception e) {
                    Display.getDefault().asyncExec(new Runnable() {
                        @Override
                        public void run() {
                            Status status = new Status(IStatus.ERROR, Activator.PLUGIN_ID, "An error occured while creating preview.", e);
                            StatusManager.getManager().handle(status, StatusManager.SHOW | StatusManager.LOG);
                        }
                    });
                }
            }
        }
    }

    public FontEditor() {
        super("font");
    }

    @Override
    public void dispose() {
        super.dispose();
        if (previewThread != null) {
            previewThread.run = false;
            previewThread.interrupt();
        }
    }

    @Override
    public void createPreviewControl(Composite parent) {
        scrolledComposite = new ScrolledComposite(parent, SWT.BORDER | SWT.V_SCROLL | SWT.H_SCROLL);
        scrolledComposite.setLayout(new FillLayout());
        canvas = new Canvas(scrolledComposite, SWT.NONE);
        scrolledComposite.setContent(canvas);

        canvas.addPaintListener(new PaintListener() {

            @Override
            public void paintControl(PaintEvent e) {
                e.gc.drawOval(0, 0, 100 - 1, 100 - 1);
                if (image != null) {
                    e.gc.drawImage(image, 0, 0);
                }
            }
        });

        root = getContentRoot();
        if (root != null) {
            previewThread = new PreviewThread();
            previewThread.start();
        }
    }

    @Override
    public void updatePreview() {
        MessageNode message = getMessage();
        ByteArrayOutputStream out = new ByteArrayOutputStream(4 * 1024);
        try {
            message.build().writeTo(out);
            FontDesc fontDesc = FontDesc.parseFrom(out.toByteArray());
            if (fontDesc.getFont() == null)
                return;

            previewThread.queue.offer(fontDesc);

        } catch (IOException e) {
            if (!previewErrorLogged) {
                previewErrorLogged = true;
                Activator.logException(e);
            }
        }
    }

    static ImageData convertToSWT(BufferedImage bufferedImage) {
        ImageData data = new ImageData(bufferedImage.getWidth(), bufferedImage.getHeight(), 24, new PaletteData(0xff0000, 0xff00, 0xff));
        for (int y = 0; y < data.height; y++) {
            for (int x = 0; x < data.width; x++) {
                int rgb = bufferedImage.getRGB(x, y);
                data.setPixel(x, y, rgb);
            }
        }
        return data;
    }
}
