package com.dynamo.cr.properties.descriptors;

import java.lang.reflect.Method;

import org.eclipse.core.commands.operations.IUndoableOperation;
import org.eclipse.jface.viewers.ArrayContentProvider;
import org.eclipse.jface.viewers.ComboViewer;
import org.eclipse.jface.viewers.ISelection;
import org.eclipse.jface.viewers.ISelectionChangedListener;
import org.eclipse.jface.viewers.IStructuredSelection;
import org.eclipse.jface.viewers.SelectionChangedEvent;
import org.eclipse.jface.viewers.StructuredSelection;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Control;

import com.dynamo.cr.properties.IPropertyEditor;
import com.dynamo.cr.properties.IPropertyModel;
import com.dynamo.cr.properties.IPropertyObjectWorld;
import com.dynamo.cr.properties.PropertyDesc;
import com.dynamo.cr.properties.PropertyUtil;
import com.google.protobuf.ProtocolMessageEnum;


public class ProtoEnumDesc<T, U extends IPropertyObjectWorld> extends PropertyDesc<T, U> {

    private Enum<?>[] enumValues;

    public ProtoEnumDesc(Class<? extends ProtocolMessageEnum> type, String id, String name) {
        super(id, name);
        Method values;
        try {
            values = type.getMethod("values");
            this.enumValues = (Enum<?>[]) values.invoke(null);
        } catch (Throwable  e) {
            throw new RuntimeException(e);
        }
    }

    private class Editor implements IPropertyEditor<T, U>, ISelectionChangedListener {
        private IPropertyModel<T, U>[] models;
        private ComboViewer viewer;
        Editor(Composite parent) {
            viewer = new ComboViewer(parent, SWT.READ_ONLY);
            viewer.addSelectionChangedListener(this);
            viewer.setContentProvider(new ArrayContentProvider());
            viewer.setInput(enumValues);
        }

        @Override
        public Control getControl() {
            return viewer.getControl();
        }

        @Override
        public void refresh() {
            Enum<?> firstValue = (Enum<?>) models[0].getPropertyValue(getId());
            for (int i = 1; i < models.length; ++i) {
                Enum<?> value = (Enum<?>) models[i].getPropertyValue(getId());
                if (!firstValue.equals(value)) {
                    viewer.setSelection(null);
                    return;
                }
            }
            viewer.setSelection(new StructuredSelection(firstValue));
        }

        @Override
        public void setModels(IPropertyModel<T, U>[] models) {
            this.models = models;
        }

        @Override
        public void selectionChanged(SelectionChangedEvent event) {
            ISelection selection = event.getSelection();
            if (selection.isEmpty())
                return;

            IStructuredSelection structSelection = (IStructuredSelection) selection;
            Enum<?> value = (Enum<?>) structSelection.getFirstElement();

            IUndoableOperation combinedOperation = PropertyUtil.setProperty(models, getId(), value);
            if (combinedOperation != null)
                models[0].getCommandFactory().execute(combinedOperation, models[0].getWorld());
        }
    }

    @Override
    public IPropertyEditor<T, U> createEditor(Composite parent) {
        return new Editor(parent);
    }

}
