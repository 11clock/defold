package com.dynamo.bob.pipeline;

import com.dynamo.bob.BuilderParams;

public class ScriptBuilders {
    @BuilderParams(name = "Lua", inExts = ".lua", outExt = ".luac")
    public static class LuaScriptBuilder extends LuaBuilder {}

    @BuilderParams(name = "Script", inExts = ".script", outExt = ".scriptc")
    public static class ScriptBuilder extends LuaBuilder {}
}
