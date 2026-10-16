using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

// Editor-only, isolated-project validation. Not compiled into the Windows Player.
public static class RecordsFontValidation
{
    public static void Validate()
    {
        string root=Directory.GetParent(Application.dataPath).FullName;
        string output=Path.Combine(Directory.GetParent(root).FullName,"font_validation.txt");
        string text=File.ReadAllText(Path.Combine(root,"Assets/Scripts/ControlStudio/ControlStudioRecordingPanel.cs"))+File.ReadAllText(Path.Combine(root,"Assets/Scripts/ControlStudio/ControlStudioUartPanel.cs"));
        var chars=text.Where(c=>c>=0xac00&&c<=0xd7a3 || c=='·' || c=='—' || c=='~').Distinct().ToArray();
        Font font=Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
        font.RequestCharactersInTexture(new string(chars),14);
        var missing=chars.Where(c=>!font.HasCharacter(c)||!font.GetCharacterInfo(c,out _,14)).ToArray();
        var log=new List<string>{"Unity "+Application.unityVersion,"Unchanged Records font: LegacyRuntime.ttf","Unique localized glyphs="+chars.Length,"Missing="+new string(missing)};
        foreach(char c in chars)log.Add((missing.Contains(c)?"FAIL":"PASS")+" glyph U+"+((int)c).ToString("X4")+" "+c);
        log.Add(missing.Length==0?"RESULT=PASS":"RESULT=FAIL");File.WriteAllLines(output,log);
        EditorApplication.Exit(missing.Length==0?0:2);
    }
}
