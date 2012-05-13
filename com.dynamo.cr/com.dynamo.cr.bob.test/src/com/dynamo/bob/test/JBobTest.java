package com.dynamo.bob.test;

import static org.hamcrest.CoreMatchers.is;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.matchers.JUnitMatchers.hasItem;

import java.io.IOException;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

import org.apache.commons.io.FilenameUtils;
import org.apache.commons.lang3.StringUtils;
import org.eclipse.core.runtime.NullProgressMonitor;
import org.eclipse.core.runtime.Platform;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.osgi.framework.Bundle;

import com.dynamo.bob.AbstractFileSystem;
import com.dynamo.bob.AbstractResource;
import com.dynamo.bob.Builder;
import com.dynamo.bob.BuilderParams;
import com.dynamo.bob.ClassScanner;
import com.dynamo.bob.CommandBuilder;
import com.dynamo.bob.CompileExceptionError;
import com.dynamo.bob.CopyBuilder;
import com.dynamo.bob.IResource;
import com.dynamo.bob.Project;
import com.dynamo.bob.Task;
import com.dynamo.bob.Task.TaskBuilder;
import com.dynamo.bob.TaskResult;

public class JBobTest {
    @BuilderParams(name = "InCopyBuilder", inExts = ".in", outExt = ".out")
    public static class InCopyBuilder extends CopyBuilder {}

    @BuilderParams(name = "CBuilder", inExts = ".c", outExt = ".o")
    public static class CBuilder extends CopyBuilder {
        @Override
        public void signature(MessageDigest digest) {
            digest.update(project.option("COPTIM", "").getBytes());
        }
    }

    @BuilderParams(name = "NoOutput", inExts = ".nooutput", outExt = ".nooutputc")
    public static class NoOutputBuilder extends CopyBuilder {
        @Override
        public void build(Task<Void> task) {
        }
    }

    @BuilderParams(name = "FailingBuilder", inExts = ".in_err", outExt = ".out_err")
    public static class FailingBuilder extends Builder<Void> {
        @Override
        public Task<Void> create(IResource input) {
            return defaultTask(input);
        }

        @Override
        public void build(Task<Void> task) throws CompileExceptionError {
            throw new CompileExceptionError(task.input(0), 0, "Failed to build");
        }
    }

    @BuilderParams(name = "CreateException", inExts = ".in_ce", outExt = ".out_ce")
    public static class CreateExceptionBuilder extends Builder<Void> {
        @Override
        public Task<Void> create(IResource input) {
            throw new RuntimeException("error");
        }

        @Override
        public void build(Task<Void> task) throws CompileExceptionError {
        }
    }


    @BuilderParams(name = "DynamicBuilder", inExts = ".dynamic", outExt = ".number")
    public static class DynamicBuilder extends Builder<Void> {

        @Override
        public Task<Void> create(IResource input) throws IOException, CompileExceptionError {
            TaskBuilder<Void> builder = Task.<Void>newBuilder(this)
                    .setName(params.name())
                    .addInput(input);

            String content = new String(input.getContent());
            String[] lst = content.split("\n");
            String baseName = FilenameUtils.removeExtension(input.getPath());
            int i = 0;
            List<Task<?>> numberTasks = new ArrayList<Task<?>>();
            for (@SuppressWarnings("unused") String s : lst) {
                String numberName = String.format("%s_%d.number", baseName, i);
                IResource numberInput = input.getResource(numberName).output();
                builder.addOutput(numberInput);
                Task<?> numberTask = project.buildResource(numberInput);
                numberTasks.add(numberTask);
                ++i;
            }
            Task<Void> t = builder.build();
            for (Task<?> task : numberTasks) {
                task.setProductOf(t);
            }
            return t;
        }

        @Override
        public void build(Task<Void> task)
                throws CompileExceptionError, IOException {
            IResource input = task.input(0);
            String content = new String(input.getContent());
            String[] lst = content.split("\n");
            int i = 0;
            for (String s : lst) {
                task.output(i++).setContent(s.getBytes());
            }
        }
    }

    @BuilderParams(name = "NumberBuilder", inExts = ".number", outExt = ".numberc")
    public static class NumberBuilder extends Builder<Void> {
        @Override
        public Task<Void> create(IResource input) {
            return defaultTask(input);
        }

        @Override
        public void build(Task<Void> task) throws CompileExceptionError, IOException {
            IResource input = task.input(0);
            int number = Integer.parseInt(new String(input.getContent()));
            task.output(0).setContent(Integer.toString(number * 10).getBytes());
        }
    }

    @BuilderParams(name = "FailOnEmptyAlwaysOutput", inExts = ".foeao", outExt = ".foeaoc")
    public static class FailOnEmptyAlwaysOutputBuilder extends Builder<Void> {
        @Override
        public Task<Void> create(IResource input) {
            return defaultTask(input);
        }

        @Override
        public void build(Task<Void> task) throws CompileExceptionError, IOException {
            task.output(0).setContent(new byte[0]);
            if (task.input(0).getContent().length == 0) {
                throw new CompileExceptionError(task.input(0), 0, "Failed to build");
            }
        }
    }

    public class MockResource extends AbstractResource<MockFileSystem> {

        private byte[] content;

        MockResource(MockFileSystem fileSystem, String path, byte[] content) {
            super(fileSystem, path);
            this.content = content;
        }

        @Override
        public byte[] getContent() {
            return content;
        }

        @Override
        public void setContent(byte[] content) {
            if (!isOutput()) {
                throw new IllegalArgumentException(String.format("Resource '%s' is not an output resource", this.toString()));
            }
            this.content = Arrays.copyOf(content, content.length);
        }

        // Only for testing
        public void forceSetContent(byte[] content) {
            this.content = Arrays.copyOf(content, content.length);
        }

        @Override
        public boolean exists() {
            return content != null;
        }

        @Override
        public void remove() {
            content = null;
        }
    }

    public class MockFileSystem extends AbstractFileSystem<MockFileSystem, MockResource> {

        public void addFile(String name, byte[] content) {
            name = FilenameUtils.normalize(name, true);
            resources.put(name, new MockResource(this, name, content));
        }

        public void addFile(MockResource resource) {
            this.resources.put(resource.getAbsPath(), resource);
        }

        @Override
        public IResource get(String name) {
            name = FilenameUtils.normalize(name, true);
            IResource r = resources.get(name);
            if (r == null) {
                r = new MockResource(fileSystem, name, null);
                resources.put(name, (MockResource) r);
            }
            return r;
        }
    }

    private MockFileSystem fileSystem;
    private Project project;
    private Bundle bundle;

    @Before
    public void setUp() throws Exception {
        bundle = Platform.getBundle("com.dynamo.cr.bob");
        fileSystem = new MockFileSystem();
        project = new Project(fileSystem);
        project.scanBundlePackage(bundle, "com.dynamo.bob.test");
    }

    List<TaskResult> build() throws IOException, CompileExceptionError {
        return project.build(new NullProgressMonitor(), "build");
    }

    @After
    public void tearDown() throws Exception {
    }

    @Test
    public void testFilePackageScan() throws Exception {
        Set<String> classes = ClassScanner.scanBundle(bundle, "com.dynamo.bob.test");
        assertThat(classes, hasItem("com.dynamo.bob.test.JBobTest"));
    }

    @Test
    public void testCopy() throws Exception {
        fileSystem.addFile("test.in", "test data".getBytes());
        project.setInputs(Arrays.asList("test.in"));
        List<TaskResult> result = build();
        assertThat(result.size(), is(1));
        IResource testOut = fileSystem.get("test.out").output();
        assertNotNull(testOut);
        assertThat(new String(testOut.getContent()), is("test data"));
    }

    @Test
    public void testAbsPath() throws Exception {
        fileSystem.addFile("/root/test.in", "test data".getBytes());
        project.setInputs(Arrays.asList("/root/test.in"));
        List<TaskResult> result = build();
        assertThat(result.size(), is(1));
        IResource testOut = fileSystem.get("/root/test.out").output();
        assertThat(testOut.exists(), is(true));
        assertThat(new String(testOut.getContent()), is("test data"));
    }

    @Test
    public void testChangeInput() throws Exception {
        fileSystem.addFile("test.in", "test data".getBytes());
        project.setInputs(Arrays.asList("test.in"));
        List<TaskResult> result;

        // build
        result = build();
        assertThat(result.size(), is(1));

        // rebuild with same input
        result = build();
        assertThat(result.size(), is(0));

        // rebuild with new input
        MockResource testIn = (MockResource) fileSystem.get("test.in");
        testIn.forceSetContent("test data prim".getBytes());
        result = build();
        assertThat(result.size(), is(1));
    }

    @Test
    public void testRemoveOutput() throws Exception {
        fileSystem.addFile("test.in", "test data".getBytes());
        project.setInputs(Arrays.asList("test.in"));
        List<TaskResult> result;

        // build
        result = build();
        assertThat(result.size(), is(1));

        // remove output
        fileSystem.get("test.out").output().remove();

        // rebuild
        result = build();
        assertThat(result.size(), is(1));
    }

    @Test
    public void testRemoveGeneratedOutput() throws Exception {
        fileSystem.addFile("test.dynamic", "1\n2\n".getBytes());
        project.setInputs(Arrays.asList("test.dynamic"));

        // build
        List<TaskResult> result = build();
        assertThat(result.size(), is(3));

        // remove generated output, ie input to another task
        fileSystem.get("test_0.numberc").output().remove();

        // rebuild
        result = build();
        assertThat(result.size(), is(1));
    }

    @Test
    public void testCompileError() throws Exception {
        fileSystem.addFile("test.in_err", "test data_err".getBytes());
        project.setInputs(Arrays.asList("test.in_err"));
        List<TaskResult> result;

        // build
        result = build();
        assertThat(result.size(), is(1));
        assertFalse(result.get(0).isOk());

        // build again
        result = build();
        assertThat(result.size(), is(1));
        assertFalse(result.get(0).isOk());
    }

    @Test(expected=CompileExceptionError.class)
    public void testCreateError() throws Exception {
        fileSystem.addFile("test.in_ce", "test".getBytes());
        project.setInputs(Arrays.asList("test.in_ce"));
        // build
        build();
    }

    @Test
    public void testMissingOutput() throws Exception {
        fileSystem.addFile("test.nooutput", "test data".getBytes());
        project.setInputs(Arrays.asList("test.nooutput"));
        List<TaskResult> result = build();
        assertThat(result.size(), is(1));
        assertFalse(result.get(0).isOk());
    }

    String getResourceString(String name) throws IOException {
        return new String(fileSystem.get(name).output().getContent());
    }

    @SuppressWarnings("rawtypes")
    @Test
    public void testDynamic() throws Exception {
        fileSystem.addFile("test.dynamic", "1\n2\n".getBytes());
        project.setInputs(Arrays.asList("test.dynamic"));
        List<TaskResult> result = build();
        assertThat(result.size(), is(3));
        assertThat(getResourceString("test_0.numberc"), is("10"));
        assertThat(getResourceString("test_1.numberc"), is("20"));
        assertThat(result.get(1).getTask().getProductOf(), is((Task) result.get(0).getTask()));
        assertThat(result.get(2).getTask().getProductOf(), is((Task) result.get(0).getTask()));
    }


    @Test
    public void testChangeOptions() throws Exception {
        fileSystem.addFile("test.c", "f();".getBytes());
        project.setInputs(Arrays.asList("test.c"));
        List<TaskResult> result;

        // build
        result = build();
        assertThat(result.size(), is(1));

        // rebuild with new option
        project.setOption("COPTIM", "-O2");
        result = build();
        assertThat(result.size(), is(1));

        // rebuild with same option
        result = build();
        assertThat(result.size(), is(0));
    }

    @SuppressWarnings("unchecked")
    @Test
    public void testCommandSubstitute1() throws Exception {
        Map<String, Object> p1 = new HashMap<String, Object>();
        p1.put("CC", "gcc");
        p1.put("COPTIM", "-O2");

        Map<String, Object> p2 = new HashMap<String, Object>();
        p2.put("INPUTS", Arrays.asList("a.c", "b.c"));
        p2.put("OUTPUTS", Arrays.asList("x.o"));
        p2.put("COPTIM", "-O0");

        List<String> lst = CommandBuilder.substitute("${CC} ${COPTIM} -c ${INPUTS} -o ${OUTPUTS[0]}", p1, p2);
        assertThat("gcc -O0 -c a.c b.c -o x.o", is(StringUtils.join(lst, " ")));

        List<String> lst2 = CommandBuilder.substitute("${CC} ${COPTIM} -c ${INPUTS[1]} -o ${OUTPUTS[0]}", p1, p2);
        assertThat("gcc -O0 -c b.c -o x.o", is(StringUtils.join(lst2, " ")));
    }

    @Test
    public void testCompileErrorOutputCreated() throws Exception {
        fileSystem.addFile("test.foeao", "test".getBytes());
        project.setInputs(Arrays.asList("test.foeao"));
        List<TaskResult> result;

        // build
        result = build();
        assertThat(result.size(), is(1));
        assertTrue(result.get(0).isOk());

        fileSystem.addFile("test.foeao", "".getBytes());

        // fail build
        result = build();
        assertThat(result.size(), is(1));
        assertFalse(result.get(0).isOk());

        fileSystem.addFile("test.foeao", "test".getBytes());

        // remedy build
        result = build();
        assertThat(result.size(), is(1));
        assertTrue(result.get(0).isOk());
    }
}

