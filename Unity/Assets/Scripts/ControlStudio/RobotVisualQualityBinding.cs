using System.Collections.Generic;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Visual mesh/material bindings only. Source transforms, rig and collider geometry stay intact.
    public sealed class RobotVisualQualityBinding
    {
        readonly Dictionary<MeshFilter, Mesh> originals = new Dictionary<MeshFilter, Mesh>();
        readonly Dictionary<Renderer, Material[]> materials = new Dictionary<Renderer, Material[]>();
        readonly Dictionary<Renderer, uint> lightLayers = new Dictionary<Renderer, uint>();
        readonly Mesh roundedBox;
        readonly Mesh cylinder;

        public RobotVisualQualityBinding()
        {
            roundedBox = Resources.Load<Mesh>("VisualProfiles/HumanoidRobot/RoundedMechanicalBox");
            var source = Resources.Load<Mesh>("VisualProfiles/HumanoidRobot/BeveledMechanicalCylinder");
            if (source != null)
            {
                cylinder = Object.Instantiate(source);
                cylinder.name = "Machined cylinder / Unity primitive bounds / visual only";
                // Existing hard-surface asset has radius 1; Unity Cylinder radius is 0.5.
                // Adapt vertices, never the joint/mesh Transform or collider.
                var vertices = cylinder.vertices;
                for (int i=0;i<vertices.Length;i++) { vertices[i].x*=.5f; vertices[i].z*=.5f; }
                cylinder.vertices=vertices;
                // The reused cylinder asset has inward cap winding. Repair the runtime copy only.
                var triangles=cylinder.triangles;
                for(int i=triangles.Length-32*2*3;i<triangles.Length;i+=3)
                { int index=triangles[i+1];triangles[i+1]=triangles[i+2];triangles[i+2]=index; }
                cylinder.triangles=triangles;cylinder.RecalculateNormals(); cylinder.RecalculateBounds();
            }
        }

        public void Apply(Transform visualRoot, bool repairLegacyMaterials=false)
        {
            if (visualRoot==null) return;
            foreach (var renderer in visualRoot.GetComponentsInChildren<MeshRenderer>(true))
            {
                var filter=renderer.GetComponent<MeshFilter>();
                if(filter==null||filter.sharedMesh==null) continue;
                var mesh=filter.sharedMesh;
                // Product specular lights reach the mechanical robot, not backdrop or white paint.
                // White paint keeps the same key/fill/ambient policy without point-light clipping.
                bool white=false;foreach(var mat in renderer.sharedMaterials)
                    if(mat!=null&&mat.name=="HumanoidWhiteShell")white=true;
                lightLayers[renderer]=renderer.renderingLayerMask;
                renderer.renderingLayerMask=white?1u:9u;
                Mesh replacement = mesh.name=="Cube" ? roundedBox :
                    mesh.name=="Cylinder" ? cylinder : null;
                if(replacement!=null&&!originals.ContainsKey(filter))
                { originals[filter]=mesh; filter.sharedMesh=replacement; }
                if(!repairLegacyMaterials) continue;
                var before=renderer.sharedMaterials;var after=(Material[])before.Clone();bool changed=false;
                for(int i=0;i<before.Length;i++)
                {
                    var mat=before[i];if(mat==null||!mat.name.StartsWith("MAT_Robot"))continue;
                    string role=renderer.name.ToLowerInvariant(), material=mat.name.ToLowerInvariant();
                    string replacementName=role.Contains("bolt")||role.Contains("washer")?"HumanoidFastenerMetal":
                        role.Contains("shaft")||role.Contains("hub")||role.Contains("bearing")||material.Contains("silver")?"HumanoidStainlessShaft":
                        role.Contains("cable")||role.Contains("pad")?"HumanoidCableRubber":
                        role.Contains("body")||role.Contains("case")?"HumanoidDarkPolymer":"HumanoidGraphite";
                    var pbr=Resources.Load<Material>("VisualProfiles/HumanoidRobot/"+replacementName);
                    if(pbr!=null){after[i]=pbr;changed=true;}
                }
                if(changed){materials[renderer]=before;renderer.sharedMaterials=after;}
            }
        }

        public void Destroy()
        {
            foreach(var pair in originals)if(pair.Key!=null)pair.Key.sharedMesh=pair.Value;
            foreach(var pair in materials)if(pair.Key!=null)pair.Key.sharedMaterials=pair.Value;
            foreach(var pair in lightLayers)if(pair.Key!=null)pair.Key.renderingLayerMask=pair.Value;
            if(cylinder!=null)Object.Destroy(cylinder);
        }
    }
}
