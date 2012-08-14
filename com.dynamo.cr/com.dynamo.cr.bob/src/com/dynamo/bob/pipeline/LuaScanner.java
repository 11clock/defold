package com.dynamo.bob.pipeline;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class LuaScanner {

    private static Pattern multiLineCommentPattern = Pattern.compile("--\\[\\[.*?--\\]\\]",
            Pattern.DOTALL | Pattern.MULTILINE);

    private static Pattern requirePattern1 = Pattern.compile("require\\s*?\"(.*?)\"$",
            Pattern.DOTALL | Pattern.MULTILINE);

    private static Pattern requirePattern2 = Pattern.compile("require\\s*?\\(\\s*?\"(.*?)\"\\s*?\\)$",
            Pattern.DOTALL | Pattern.MULTILINE);

    private static String stripSingleLineComments(String str) {
        str = str.replace("\r", "");
        StringBuffer sb = new StringBuffer();
        String[] lines = str.split("\n");
        for (String line : lines) {
            String lineTrimmed = line.trim();
            // Strip single line comments but preserve "pure" multi-line comments
            // Note that ---[[ is a single line comment
            // You can enable a block in Lua by adding a hyphen, e.g.
            /*
             ---[[
             The block is enabled
             --]]
             */
            if (!lineTrimmed.startsWith("--") || lineTrimmed.startsWith("--[[") || lineTrimmed.startsWith("--]]")) {
                sb.append(line);
            }
            sb.append("\n");
        }
        return sb.toString();
    }

    public static List<String> scan(String str) {
        str = stripSingleLineComments(str);
        Matcher matcher = multiLineCommentPattern.matcher(str);

        StringBuffer sb = new StringBuffer();
        while (matcher.find()) {
            // Replace comment with n lines in order to preserve line indices
            int n = matcher.group().split("\n").length;
            StringBuffer lines = new StringBuffer(n);
            for (int i = 0; i < n; ++i) lines.append('\n');
            matcher.appendReplacement(sb, lines.toString());
        }
        matcher.appendTail(sb);
        String strStripped = sb.toString();

        ArrayList<String> modules = new ArrayList<String>();
        String[] lines = strStripped.split("\n");
        for (String line : lines) {
            line = line.trim();
            Matcher propMatcher1 = requirePattern1.matcher(line);
            Matcher propMatcher2 = requirePattern2.matcher(line);
            if (propMatcher1.matches()) {
                modules.add(propMatcher1.group(1));
            } else if (propMatcher2.matches()) {
                modules.add(propMatcher2.group(1));
            }
        }
        return modules;
    }

}
