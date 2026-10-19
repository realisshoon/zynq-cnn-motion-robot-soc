using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

// Reusable, asset-backed hard-surface meshes for the presentation humanoid.
internal static class HumanoidVisualGeometry
{
    const string Folder="Assets/Resources/VisualProfiles/HumanoidRobot";
    internal static Material White,Black,Polymer,Aluminum,Steel,Fastener,Rubber,Cyan,Visor;
    internal static Mesh BoxMesh,CylinderMesh,TorsoMesh;

    static Material MakeMaterial(string name,Color color,float metallic,float smoothness)
    {
        string path=Folder+"/"+name+".mat";
        var m=AssetDatabase.LoadAssetAtPath<Material>(path);
        if(m==null)
        {
            m=new Material(Shader.Find("Universal Render Pipeline/Lit")??Shader.Find("Standard")){name=name};
            AssetDatabase.CreateAsset(m,path);
        }
        m.color=color;
        if(m.HasProperty("_BaseColor"))m.SetColor("_BaseColor",color);
        if(m.HasProperty("_Metallic"))m.SetFloat("_Metallic",metallic);
        if(m.HasProperty("_Smoothness"))m.SetFloat("_Smoothness",smoothness);
        EditorUtility.SetDirty(m);return m;
    }
    static Mesh MakeMesh(string name,Func<Mesh> create)
    {
        string path=Folder+"/"+name+".asset";
        var old=AssetDatabase.LoadAssetAtPath<Mesh>(path);
        var fresh=create();fresh.name=name;
        if(old==null){AssetDatabase.CreateAsset(fresh,path);return fresh;}
        EditorUtility.CopySerialized(fresh,old);UnityEngine.Object.DestroyImmediate(fresh);
        EditorUtility.SetDirty(old);return old;
    }
    internal static void Initialize()
    {
        White=MakeMaterial("HumanoidWhiteShell",new Color(.88f,.91f,.93f),.08f,.52f);
        Black=MakeMaterial("HumanoidGraphite",new Color(.052f,.058f,.068f),.50f,.48f);
        Polymer=MakeMaterial("HumanoidDarkPolymer",new Color(.12f,.13f,.15f),.02f,.32f);
        Aluminum=MakeMaterial("HumanoidBrushedSteel",new Color(.48f,.54f,.59f),.82f,.60f);
        Steel=MakeMaterial("HumanoidStainlessShaft",new Color(.70f,.74f,.77f),.90f,.78f);
        Fastener=MakeMaterial("HumanoidFastenerMetal",new Color(.32f,.37f,.41f),.82f,.65f);
        Rubber=MakeMaterial("HumanoidCableRubber",new Color(.018f,.023f,.030f),0f,.22f);
        Cyan=MakeMaterial("HumanoidSensorCyan",new Color(.10f,.77f,.87f),.16f,.65f);
        Visor=MakeMaterial("HumanoidVisorGlass",new Color(.012f,.020f,.027f),.07f,.88f);
        BoxMesh=MakeMesh("RoundedMechanicalBox",CreateBox);
        CylinderMesh=MakeMesh("BeveledMechanicalCylinder",CreateCylinder);
        TorsoMesh=MakeMesh("CurvedTorsoFront",CreateTorso);
    }

    static Mesh CreateBox()
    {
        const int div=8;const float radius=.12f;
        var vertices=new List<Vector3>();var normals=new List<Vector3>();var uv=new List<Vector2>();var triangles=new List<int>();
        Vector3[] n={Vector3.right,Vector3.left,Vector3.up,Vector3.down,Vector3.forward,Vector3.back};
        Vector3[] u={Vector3.back,Vector3.forward,Vector3.right,Vector3.right,Vector3.right,Vector3.left};
        Vector3[] v={Vector3.up,Vector3.up,Vector3.back,Vector3.forward,Vector3.up,Vector3.up};
        for(int face=0;face<6;face++)
        {
            int start=vertices.Count;
            for(int y=0;y<=div;y++)for(int x=0;x<=div;x++)
            {
                Vector3 p=n[face]*.5f+u[face]*(x/(float)div-.5f)+v[face]*(y/(float)div-.5f);
                Vector3 inner=new Vector3(Mathf.Clamp(p.x,-.5f+radius,.5f-radius),
                    Mathf.Clamp(p.y,-.5f+radius,.5f-radius),Mathf.Clamp(p.z,-.5f+radius,.5f-radius));
                Vector3 delta=(p-inner).normalized;
                vertices.Add(inner+delta*radius);normals.Add(delta);uv.Add(new Vector2(x/(float)div,y/(float)div));
            }
            for(int y=0;y<div;y++)for(int x=0;x<div;x++)
            {
                int a=start+y*(div+1)+x,b=a+1,c=a+div+1,d=c+1;
                triangles.Add(a);triangles.Add(b);triangles.Add(c);
                triangles.Add(b);triangles.Add(d);triangles.Add(c);
            }
        }
        var m=new Mesh();m.SetVertices(vertices);m.SetNormals(normals);m.SetUVs(0,uv);m.SetTriangles(triangles,0);m.RecalculateBounds();return m;
    }
    static Mesh CreateCylinder()
    {
        const int sides=32;float[] y={-1f,-.94f,-.84f,.84f,.94f,1f};
        float[] radii={.79f,.95f,1f,1f,.95f,.79f};
        var vertices=new List<Vector3>();var uv=new List<Vector2>();var triangles=new List<int>();
        for(int row=0;row<y.Length;row++)for(int i=0;i<=sides;i++)
        {
            float a=i*Mathf.PI*2/sides;
            vertices.Add(new Vector3(Mathf.Cos(a)*radii[row],y[row],Mathf.Sin(a)*radii[row]));
            uv.Add(new Vector2(i/(float)sides,row/(float)(y.Length-1)));
        }
        for(int row=0;row<y.Length-1;row++)for(int i=0;i<sides;i++)
        {
            int a=row*(sides+1)+i,b=a+1,c=a+sides+1,d=c+1;
            triangles.Add(a);triangles.Add(c);triangles.Add(b);
            triangles.Add(b);triangles.Add(c);triangles.Add(d);
        }
        int low=vertices.Count;vertices.Add(Vector3.down);uv.Add(Vector2.zero);
        int high=vertices.Count;vertices.Add(Vector3.up);uv.Add(Vector2.one);
        for(int i=0;i<sides;i++)
        {
            triangles.Add(low);triangles.Add(i+1);triangles.Add(i);
            int last=(y.Length-1)*(sides+1)+i;
            triangles.Add(high);triangles.Add(last);triangles.Add(last+1);
        }
        var m=new Mesh();m.SetVertices(vertices);m.SetUVs(0,uv);m.SetTriangles(triangles,0);
        m.RecalculateNormals();m.RecalculateBounds();return m;
    }
    static Mesh CreateTorso()
    {
        const int sides=40;float[] y={-.32f,-.29f,-.22f,.14f,.25f,.30f};
        float[] rx={.24f,.29f,.36f,.38f,.33f,.27f};
        float[] rz={.10f,.15f,.19f,.19f,.15f,.10f};
        var vertices=new List<Vector3>();var uv=new List<Vector2>();var triangles=new List<int>();
        for(int row=0;row<y.Length;row++)for(int i=0;i<=sides;i++)
        {
            float angle=Mathf.Lerp(-1.73f,1.73f,i/(float)sides);
            vertices.Add(new Vector3(Mathf.Sin(angle)*rx[row],y[row],-.016f-Mathf.Cos(angle)*rz[row]));
            uv.Add(new Vector2(i/(float)sides,row/(float)(y.Length-1)));
        }
        for(int row=0;row<y.Length-1;row++)for(int i=0;i<sides;i++)
        {
            int a=row*(sides+1)+i,b=a+1,c=a+sides+1,d=c+1;
            triangles.Add(a);triangles.Add(c);triangles.Add(b);
            triangles.Add(b);triangles.Add(c);triangles.Add(d);
        }
        var m=new Mesh();m.SetVertices(vertices);m.SetUVs(0,uv);m.SetTriangles(triangles,0);
        m.RecalculateNormals();m.RecalculateBounds();return m;
    }

    internal static Transform Part(string name,Mesh mesh,Transform parent,Vector3 position,Vector3 scale,Material material)
    {
        var t=new GameObject(name).transform;t.SetParent(parent,false);t.localPosition=position;t.localScale=scale;
        t.gameObject.AddComponent<MeshFilter>().sharedMesh=mesh;
        t.gameObject.AddComponent<MeshRenderer>().sharedMaterial=material;return t;
    }
    internal static Transform Box(string name,Transform parent,Vector3 position,Vector3 size,Material material)
        =>Part(name,BoxMesh,parent,position,size,material);
    internal static Transform Cylinder(string name,Transform parent,Vector3 position,Vector3 size,Material material)
        =>Part(name,CylinderMesh,parent,position,size,material);
    internal static Transform WorldPart(string name,Mesh mesh,Transform parent,Vector3 center,Vector3 size,Material material)
    {
        var t=Part(name,mesh,parent,Vector3.zero,Vector3.one,material);t.position=center;
        Vector3 parentScale=parent.lossyScale;
        t.localScale=new Vector3(size.x/parentScale.x,size.y/parentScale.y,size.z/parentScale.z);return t;
    }
    internal static Transform Rod(string name,Transform parent,Vector3 a,Vector3 b,float radius,Material material)
    {
        var t=WorldPart(name,CylinderMesh,parent,(a+b)*.5f,new Vector3(radius,(b-a).magnitude*.5f,radius),material);
        t.rotation=Quaternion.FromToRotation(Vector3.up,b-a);return t;
    }
    internal static void Flange(string name,Transform parent,Vector3 worldCenter,float radius,float thickness,int side)
    {
        var housing=WorldPart(name+" housing",CylinderMesh,parent,worldCenter,
            new Vector3(radius,thickness*.5f,radius),Black);housing.rotation=Quaternion.Euler(0,0,90);
        var face=WorldPart(name+" bearing face",CylinderMesh,parent,worldCenter+Vector3.right*side*thickness*.56f,
            new Vector3(radius*.77f,thickness*.12f,radius*.77f),Aluminum);face.rotation=housing.rotation;
        var shaft=WorldPart(name+" shaft",CylinderMesh,parent,worldCenter+Vector3.right*side*thickness*.72f,
            new Vector3(radius*.27f,thickness*.11f,radius*.27f),Steel);shaft.rotation=housing.rotation;
        for(int i=0;i<6;i++)
        {
            float angle=i*Mathf.PI/3;
            Vector3 p=worldCenter+Vector3.right*side*thickness*.70f+
                new Vector3(0,Mathf.Cos(angle)*radius*.59f,Mathf.Sin(angle)*radius*.59f);
            var washer=WorldPart(name+" washer "+i,CylinderMesh,parent,p,
                new Vector3(.010f,.003f,.010f),Aluminum);washer.rotation=housing.rotation;
            var bolt=WorldPart(name+" fastener "+i,CylinderMesh,parent,p+Vector3.right*side*.003f,
                new Vector3(.006f,.004f,.006f),Fastener);bolt.rotation=housing.rotation;
        }
    }
}
