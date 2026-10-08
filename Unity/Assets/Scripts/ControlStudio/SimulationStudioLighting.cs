using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

namespace HumanMotion.ControlStudio
{
    // Runtime studio illumination only; robot mesh/PBR ownership is separate.
    public sealed class SimulationStudioLighting
    {
        readonly Dictionary<Light, bool> previousLights = new Dictionary<Light, bool>();
        readonly AmbientMode ambientMode = RenderSettings.ambientMode;
        readonly Color sky = RenderSettings.ambientSkyColor, equator = RenderSettings.ambientEquatorColor,
            ground = RenderSettings.ambientGroundColor;
        readonly SphericalHarmonicsL2 ambientProbe = RenderSettings.ambientProbe;
        readonly DefaultReflectionMode reflectionMode = RenderSettings.defaultReflectionMode;
        readonly Texture previousReflection = RenderSettings.customReflectionTexture;
        readonly float reflectionIntensity = RenderSettings.reflectionIntensity;
        readonly Light previousSun = RenderSettings.sun;
        readonly Cubemap studioReflection;
        readonly Transform lightRig;
        readonly Light productKey, productFill, productRim;

        public SimulationStudioLighting(Transform parent)
        {
            lightRig = parent;
            foreach (var light in Object.FindObjectsByType<Light>(FindObjectsInactive.Include, FindObjectsSortMode.None))
                if (light.gameObject.scene == parent.gameObject.scene)
                { previousLights[light] = light.enabled; light.enabled = false; }

            var key = Directional(parent, "Studio Key / soft shadow", new Vector3(42, 35, 0), new Color(1,.98f,.95f), .9f);
            key.shadows = LightShadows.Soft; key.shadowStrength = .50f;
            key.shadowBias = .015f; key.shadowNormalBias = .035f;
            Directional(parent, "Studio Fill / camera right front", new Vector3(25, -40, 0), new Color(.86f,.93f,1), .40f);
            Directional(parent, "Studio TopFill / environment", new Vector3(80, 0, 0), new Color(.9f,.95f,1), .20f);
            Directional(parent, "Studio Rim / back upper", new Vector3(32, 150, 0), new Color(.88f,.94f,1), .15f);
            productKey=ProductPoint(parent,"Product Key / metal edge",new Color(.90f,.95f,1));
            productFill=ProductPoint(parent,"Product Fill / cool softbox",new Color(.60f,.77f,1));
            productRim=ProductPoint(parent,"Product Rim / steel separation",new Color(.80f,.94f,1));
            var poolObject = new GameObject("Studio FloorFill / broad cool pool",typeof(Light));
            poolObject.transform.SetParent(parent,false);poolObject.transform.localPosition=new Vector3(.8f,1.4f,-.7f);
            var pool = poolObject.GetComponent<Light>();pool.type=LightType.Point;pool.color=new Color(.72f,.84f,1);
            pool.intensity=3.5f;pool.range=5f;pool.shadows=LightShadows.Soft;pool.shadowStrength=.6f;
            pool.shadowBias=.01f;pool.shadowNormalBias=.02f;
            var poolData=pool.GetUniversalAdditionalLightData();
            poolData.renderingLayers=2; // Lights the ground, not the white shell.
            poolData.customShadowLayers=true;poolData.shadowRenderingLayers=1; // Robot casts onto ground.
            RenderSettings.sun = key;
            RenderSettings.ambientMode = AmbientMode.Trilight;
            RenderSettings.ambientSkyColor = new Color(.42f,.47f,.55f);
            RenderSettings.ambientEquatorColor = new Color(.32f,.36f,.43f);
            RenderSettings.ambientGroundColor = new Color(.20f,.23f,.28f);
            var ambient = new SphericalHarmonicsL2();
            ambient.AddAmbientLight(new Color(.28f,.32f,.38f));
            ambient.AddDirectionalLight(Vector3.up, new Color(.20f,.23f,.28f), .6f);
            RenderSettings.ambientProbe = ambient;

            // Deterministic HDR studio environment, independent of realtime probe support.
            // This is a generated softbox cubemap, not a captured/baked scene reflection.
            studioReflection = CreateStudioReflection();
            RenderSettings.defaultReflectionMode = DefaultReflectionMode.Custom;
            RenderSettings.customReflectionTexture = studioReflection;
            RenderSettings.reflectionIntensity = .90f;
            var probeObject = new GameObject("Studio softbox reflection / custom");
            probeObject.transform.SetParent(parent, false);
            var probe = probeObject.AddComponent<ReflectionProbe>();
            probe.mode = ReflectionProbeMode.Custom; probe.customBakedTexture = studioReflection;
            probe.size = new Vector3(12,8,12); probe.center = Vector3.up * 2;
            probe.intensity = .90f; probe.boxProjection = false; probe.blendDistance = 1;
        }

        static Light ProductPoint(Transform parent,string name,Color color)
        {
            var go=new GameObject(name,typeof(Light));go.transform.SetParent(parent,false);
            var light=go.GetComponent<Light>();light.type=LightType.Point;light.color=color;
            light.GetUniversalAdditionalLightData().renderingLayers=8;
            return light;
        }

        public void PlaceRobot(Bounds bounds)
        {
            float span=Mathf.Max(.45f,bounds.size.magnitude);
            Vector3 center=Vector3.up*(bounds.center.y-lightRig.position.y);
            productKey.transform.localPosition=center+new Vector3(.60f,.65f,-.75f)*span;
            productFill.transform.localPosition=center+new Vector3(-.70f,.25f,-.25f)*span;
            productRim.transform.localPosition=center+new Vector3(0,.55f,.85f)*span;
            // Scale energy with squared distance so G51 and humanoid use the same studio policy.
            float energy=span*span/(.55f*.55f);
            productKey.intensity=1.1f*energy;productFill.intensity=.6f*energy;productRim.intensity=.8f*energy;
            productKey.range=productFill.range=productRim.range=span*3.3f;
        }

        public void ApplyView(Camera camera)
        {
            // Same rig/energy for every profile and UI state; only azimuth tracks the view.
            // Local +35 yaw puts the source camera-left (the light travels toward the robot).
            if(camera!=null)lightRig.rotation=Quaternion.Euler(0,camera.transform.eulerAngles.y,0);
        }

        static Light Directional(Transform parent, string name, Vector3 angles, Color color, float intensity)
        {
            var item = new GameObject(name, typeof(Light)); item.transform.SetParent(parent, false);
            item.transform.localRotation = Quaternion.Euler(angles);
            var light = item.GetComponent<Light>(); light.type = LightType.Directional;
            light.GetUniversalAdditionalLightData().renderingLayers=3; // Robot/default (1) and floor (2).
            light.color = color; light.intensity = intensity; light.shadows = LightShadows.None;
            return light;
        }

        static Cubemap CreateStudioReflection()
        {
            const int size = 128;
            var cube = new Cubemap(size, TextureFormat.RGBAHalf, true)
                { name = "Simulation studio HDR softboxes", filterMode = FilterMode.Trilinear };
            for (int face=0; face<6; face++)
            {
                var pixels = new Color[size*size];
                for (int y=0; y<size; y++) for (int x=0; x<size; x++)
                {
                    float u = 2f*(x+.5f)/size-1, v = 2f*(y+.5f)/size-1;
                    Vector3 d;
                    switch ((CubemapFace)face)
                    {
                        case CubemapFace.PositiveX: d=new Vector3(1,-v,-u); break;
                        case CubemapFace.NegativeX: d=new Vector3(-1,-v,u); break;
                        case CubemapFace.PositiveY: d=new Vector3(u,1,v); break;
                        case CubemapFace.NegativeY: d=new Vector3(u,-1,-v); break;
                        case CubemapFace.PositiveZ: d=new Vector3(u,-v,1); break;
                        default: d=new Vector3(-u,-v,-1); break;
                    }
                    d.Normalize();
                    float ceiling = Mathf.SmoothStep(-.3f,.8f,d.y);
                    Color color = Color.Lerp(new Color(.07f,.09f,.12f), new Color(.28f,.32f,.38f), ceiling);
                    float key = Mathf.Pow(Mathf.Max(0,Vector3.Dot(d,new Vector3(-.5f,.65f,-.55f).normalized)),80);
                    float fill = Mathf.Pow(Mathf.Max(0,Vector3.Dot(d,new Vector3(.7f,.35f,-.6f).normalized)),18);
                    float rim = Mathf.Pow(Mathf.Max(0,Vector3.Dot(d,new Vector3(.25f,.6f,.8f).normalized)),35);
                    color += new Color(5f,5.2f,5.6f)*key + new Color(.6f,.75f,1)*fill + new Color(2f,2.3f,2.6f)*rim;
                    color.a = 1; pixels[y*size+x] = color;
                }
                cube.SetPixels(pixels,(CubemapFace)face);
            }
            cube.Apply(true,true); return cube;
        }

        public void Destroy()
        {
            foreach (var pair in previousLights) if (pair.Key != null) pair.Key.enabled = pair.Value;
            RenderSettings.sun = previousSun; RenderSettings.ambientMode = ambientMode;
            RenderSettings.ambientSkyColor = sky; RenderSettings.ambientEquatorColor = equator;
            RenderSettings.ambientGroundColor = ground; RenderSettings.ambientProbe = ambientProbe;
            RenderSettings.defaultReflectionMode = reflectionMode;
            RenderSettings.customReflectionTexture = previousReflection; RenderSettings.reflectionIntensity = reflectionIntensity;
            Object.Destroy(studioReflection);
        }
    }
}
