using System.Collections.Generic;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Independent of the work-cell. It supplies only a presentation floor, grid and light.
    public sealed class SimulationEnvironment
    {
        readonly GameObject root;
        readonly List<Material> materials = new List<Material>();
        readonly Camera camera;
        readonly Mesh groundMesh;
        readonly Color originalBackground;
        readonly CameraClearFlags originalClearFlags;
        readonly Texture2D gridTexture;
        readonly SimulationStudioLighting studioLighting;
        public Transform Root => root.transform;

        public SimulationEnvironment(Camera sceneCamera)
        {
            camera = sceneCamera;
            if (camera != null) { originalBackground = camera.backgroundColor; originalClearFlags = camera.clearFlags; }
            root = new GameObject("SimulationEnvironmentRoot");
            var ground = new GameObject("GroundPlane", typeof(MeshFilter), typeof(MeshRenderer));
            ground.transform.SetParent(root.transform, false);
            groundMesh = new Mesh { name = "Simulation floor quad" };
            groundMesh.vertices = new[] { new Vector3(-.5f,-.5f), new Vector3(.5f,-.5f),
                new Vector3(-.5f,.5f), new Vector3(.5f,.5f) };
            groundMesh.uv = new[] { Vector2.zero, Vector2.right, Vector2.up, Vector2.one };
            groundMesh.triangles = new[] { 0,1,2,2,1,3 };
            groundMesh.RecalculateNormals();
            ground.GetComponent<MeshFilter>().sharedMesh = groundMesh;
            ground.transform.localRotation = Quaternion.Euler(-90f, 0f, 0f);
            ground.transform.localScale = new Vector3(12f, 12f, 1f);
            var floorMaterial=Material("Simulation neutral grey floor",
                "Universal Render Pipeline/Lit", new Color(.36f, .39f, .43f), 0f, .18f);
            ground.GetComponent<MeshRenderer>().sharedMaterial=floorMaterial;
            ground.GetComponent<MeshRenderer>().renderingLayerMask=2;

            var grid = new GameObject("Grid");
            grid.transform.SetParent(root.transform, false);
            // Mipmapped floor pattern avoids far-view z-fighting/subpixel line meshes.
            gridTexture=new Texture2D(512,512,TextureFormat.RGBA32,true){name="Simulation grid pattern",wrapMode=TextureWrapMode.Repeat,filterMode=FilterMode.Trilinear,anisoLevel=8};
            var pixels=new Color[512*512];
            for(int y=0;y<512;y++)for(int x=0;x<512;x++){
                float dx=Mathf.Abs(Mathf.Repeat(x+51.2f,102.4f)-51.2f),dy=Mathf.Abs(Mathf.Repeat(y+51.2f,102.4f)-51.2f);
                float minor=1-Mathf.SmoothStep(.35f,1.25f,Mathf.Min(dx,dy));
                float major=1-Mathf.SmoothStep(.9f,2.2f,Mathf.Min(Mathf.Min(x,512-x),Mathf.Min(y,512-y)));
                float shade=1-Mathf.Max(minor*.44f,major*.70f);pixels[y*512+x]=new Color(shade,shade,shade,1);
            }
            gridTexture.SetPixels(pixels);gridTexture.Apply(true,true);
            string textureSlot=floorMaterial.HasProperty("_BaseMap")?"_BaseMap":"_MainTex";
            floorMaterial.SetTexture(textureSlot,gridTexture);floorMaterial.SetTextureScale(textureSlot,new Vector2(12f,12f));
            // Neutral simulation stage, shared by Startup, Main and Presentation.
            // These are backdrop surfaces, not work-cell/tool effect panels.
            var backdrop=new GameObject("SimulationBackdrop").transform;backdrop.SetParent(root.transform,false);
            var wallMaterial=Material("Simulation cool blue-grey wall","Universal Render Pipeline/Lit",new Color(.31f,.34f,.39f),0f,.08f);
            BackdropPart(backdrop,"RearWall",new Vector3(0,4f,3f),Quaternion.Euler(0,180,0),new Vector3(24f,8f,1),wallMaterial);
            var lighting = new GameObject("SimulationLighting");
            lighting.transform.SetParent(root.transform, false);
            studioLighting = new SimulationStudioLighting(lighting.transform);
            ApplyBackground(true);
        }

        void BackdropPart(Transform parent,string name,Vector3 position,Quaternion rotation,Vector3 scale,Material material)
        {
            var part=new GameObject(name,typeof(MeshFilter),typeof(MeshRenderer));part.transform.SetParent(parent,false);
            part.transform.localPosition=position;part.transform.localRotation=rotation;part.transform.localScale=scale;
            part.GetComponent<MeshFilter>().sharedMesh=groundMesh;part.GetComponent<MeshRenderer>().sharedMaterial=material;
            part.GetComponent<MeshRenderer>().shadowCastingMode=UnityEngine.Rendering.ShadowCastingMode.Off;
        }

        Material Material(string name, string shaderName, Color color, float metallic, float smoothness)
        {
            var shader = Shader.Find(shaderName) ?? Shader.Find("Standard");
            var material = new Material(shader) { name = name, color = color };
            if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", color);
            if (material.HasProperty("_Metallic")) material.SetFloat("_Metallic", metallic);
            if (material.HasProperty("_Smoothness")) material.SetFloat("_Smoothness", smoothness);
            materials.Add(material);
            return material;
        }

        public void PlaceUnder(Transform visualRoot)
        {
            if (visualRoot == null) return;
            bool found = false;
            var bounds = new Bounds(visualRoot.position, Vector3.zero);
            foreach (var renderer in visualRoot.GetComponentsInChildren<Renderer>(true))
            {
                if (!renderer.enabled || !renderer.gameObject.activeInHierarchy) continue;
                if (!found) { bounds = renderer.bounds; found = true; }
                else bounds.Encapsulate(renderer.bounds);
            }
            if (found)
            {
                root.transform.position = new Vector3(bounds.center.x, bounds.min.y - .001f, bounds.center.z);
                studioLighting.PlaceRobot(bounds);
            }
        }

        public void ApplyBackground(bool robotOnly)
        {
            if (camera == null) return;
            studioLighting.ApplyView(camera);
            camera.clearFlags = robotOnly ? CameraClearFlags.SolidColor : originalClearFlags;
            camera.backgroundColor = robotOnly ? new Color(.36f, .40f, .46f) : originalBackground;
        }

        public void Destroy()
        {
            studioLighting.Destroy();
            if (camera != null) { camera.clearFlags = originalClearFlags; camera.backgroundColor = originalBackground; }
            if (root != null) Object.Destroy(root);
            if (groundMesh != null) Object.Destroy(groundMesh);
            if (gridTexture != null) Object.Destroy(gridTexture);
            foreach (var material in materials) if (material != null) Object.Destroy(material);
        }
    }
}
