using System.Collections.Generic;
using UnityEngine;

namespace HumanMotion.ControlStudio
{
    // Uses the existing G51 mechanical meshes. It never changes pivots, colliders or commands.
    public sealed class G51IndustrialVisual : MonoBehaviour
    {
        readonly Dictionary<Renderer, Material[]> original = new Dictionary<Renderer, Material[]>();
        readonly Dictionary<MeshFilter, Mesh> originalMeshes = new Dictionary<MeshFilter, Mesh>();
        readonly List<Mesh> bevelMeshes = new List<Mesh>();
        readonly List<Material> materials = new List<Material>();
        Transform lighting;
        Material painted, brushed, shaft, fastener, polymer, accent;

        public bool Initialize(Demo06RealisticVisualAdapter adapter)
        {
            if (adapter == null || !adapter.IsConfigured) return false;
            painted = Pbr("G51 dark painted metal", new Color(.085f,.097f,.110f), .55f, .43f);
            brushed = Pbr("G51 brushed aluminum", new Color(.47f,.52f,.56f), .88f, .60f);
            shaft = Pbr("G51 polished shaft steel", new Color(.67f,.71f,.74f), .92f, .77f);
            fastener = Pbr("G51 fastener metal", new Color(.36f,.40f,.43f), .86f, .61f);
            polymer = Pbr("G51 servo polymer", new Color(.045f,.052f,.062f), .08f, .33f);
            accent = Pbr("G51 instrument accent", new Color(.055f,.45f,.48f), .32f, .55f);

            // The source scene already contains bracket, housing, bearing, shaft and bolt geometry.
            foreach (var renderer in adapter.controller.transform.root.GetComponentsInChildren<MeshRenderer>(true))
            {
                Material chosen = Classify(renderer.transform);
                if (chosen == null) continue;
                original[renderer] = renderer.sharedMaterials;
                var replacement = new Material[renderer.sharedMaterials.Length];
                for (int i = 0; i < replacement.Length; i++) replacement[i] = chosen;
                renderer.sharedMaterials = replacement;
                var filter = renderer.GetComponent<MeshFilter>();
                if (filter != null && filter.sharedMesh != null && filter.sharedMesh.name == "Cube")
                {
                    originalMeshes[filter] = filter.sharedMesh;
                    var bevel = BeveledHousingMesh();
                    bevel.name = "G51 machined edge / visual only";
                    bevelMeshes.Add(bevel);
                    filter.sharedMesh = bevel;
                }
            }
            if (original.Count == 0) return false;
            BuildLights(adapter);
            return true;
        }

        Material Pbr(string name, Color color, float metallic, float smoothness)
        {
            var shader = Shader.Find("Universal Render Pipeline/Lit") ?? Shader.Find("Standard");
            var material = new Material(shader) { name = name, color = color };
            if (material.HasProperty("_BaseColor")) material.SetColor("_BaseColor", color);
            if (material.HasProperty("_Metallic")) material.SetFloat("_Metallic", metallic);
            if (material.HasProperty("_Smoothness")) material.SetFloat("_Smoothness", smoothness);
            materials.Add(material);
            return material;
        }

        Material Classify(Transform part)
        {
            string name = part.name.ToLowerInvariant();
            if (Contains(name,"pad","rubber","gripface","cable")) return polymer;
            if (Contains(name,"bolt","washer","fastener","hex","screw","standoff")) return fastener;
            if (Contains(name,"shaft","bearing","coupler","collar","hub","outputboss","pivotcap","servoneck")) return shaft;
            if (Contains(name,"recess","indicator","statuslight")) return accent;
            if (Contains(name,"gear","sideplate","mountplate","bracketplate","frameplate","ring","flange")) return brushed;
            if (Contains(name,"servo","case","motor","body")) return polymer;
            if (Contains(name,"base","housing","bracket","link","plate","mount","finger","jaw","frame","carrier","bridge")) return painted;
            return null; // Unknown mechanical roles retain their source material.
        }

        static bool Contains(string text, params string[] terms)
        {
            foreach (var term in terms) if (text.Contains(term)) return true;
            return false;
        }

        static Mesh BeveledHousingMesh()
        {
            const float radius = .075f;
            const float flat = .5f - radius;
            var grid = new[] { -.5f, -flat, flat, .5f };
            var normals = new[] { Vector3.right, Vector3.left, Vector3.up,
                Vector3.down, Vector3.forward, Vector3.back };
            var tangent = new[] { Vector3.up, Vector3.forward, Vector3.forward,
                Vector3.right, Vector3.right, Vector3.up };
            var bitangent = new[] { Vector3.forward, Vector3.up, Vector3.right,
                Vector3.forward, Vector3.up, Vector3.right };
            var vertices = new List<Vector3>(96);
            var vertexNormals = new List<Vector3>(96);
            var triangles = new List<int>(108);
            for (int face = 0; face < 6; face++)
            {
                int offset = vertices.Count;
                for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
                {
                    Vector3 p = normals[face] * .5f + tangent[face] * grid[x] + bitangent[face] * grid[y];
                    Vector3 core = new Vector3(Mathf.Clamp(p.x, -flat, flat),
                        Mathf.Clamp(p.y, -flat, flat), Mathf.Clamp(p.z, -flat, flat));
                    Vector3 normal = (p - core).normalized;
                    vertices.Add(core + normal * radius);
                    vertexNormals.Add(normal);
                }
                for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++)
                {
                    int a = offset + y * 4 + x, b = a + 1, c = a + 4, d = c + 1;
                    triangles.Add(a); triangles.Add(b); triangles.Add(c);
                    triangles.Add(b); triangles.Add(d); triangles.Add(c);
                }
            }
            var mesh = new Mesh();
            mesh.SetVertices(vertices);
            mesh.SetNormals(vertexNormals);
            mesh.SetTriangles(triangles, 0);
            mesh.RecalculateBounds();
            return mesh;
        }

        void BuildLights(Demo06RealisticVisualAdapter adapter)
        {
            bool found = false;
            var bounds = new Bounds(adapter.transform.position, Vector3.one * .1f);
            foreach (var renderer in adapter.controller.transform.root.GetComponentsInChildren<Renderer>(true))
            {
                if (!renderer.enabled || !renderer.gameObject.activeInHierarchy) continue;
                if (!found) { bounds = renderer.bounds; found = true; }
                else bounds.Encapsulate(renderer.bounds);
            }
            if (!found) return;
            lighting = new GameObject("G51 three-point product lighting / VISUAL ONLY").transform;
            lighting.SetParent(transform, true);
            var span = Mathf.Max(.45f, bounds.size.magnitude);
            Point("painted-metal key", bounds.center + new Vector3(span*.60f,span*.65f,-span*.75f),
                new Color(.90f,.95f,1f), 2.3f, span*3.3f);
            Point("soft cool fill", bounds.center + new Vector3(-span*.70f,span*.25f,-span*.25f),
                new Color(.60f,.77f,1f), 1.2f, span*3.0f);
            Point("steel edge rim", bounds.center + new Vector3(0,span*.55f,span*.85f),
                new Color(.80f,.94f,1f), 1.7f, span*3.0f);
        }

        void Point(string name, Vector3 position, Color color, float intensity, float range)
        {
            var go = new GameObject(name, typeof(Light));
            go.transform.SetParent(lighting, true);
            go.transform.position = position;
            var light = go.GetComponent<Light>();
            light.type = LightType.Point; light.color = color; light.intensity = intensity;
            light.range = range; light.shadows = LightShadows.Soft;
        }

        public void SetVisible(bool visible)
        {
            if (lighting != null) lighting.gameObject.SetActive(visible);
        }

        void OnDestroy()
        {
            foreach (var pair in original) if (pair.Key != null) pair.Key.sharedMaterials = pair.Value;
            foreach (var pair in originalMeshes) if (pair.Key != null) pair.Key.sharedMesh = pair.Value;
            if (lighting != null) Destroy(lighting.gameObject);
            foreach (var mesh in bevelMeshes) if (mesh != null) Destroy(mesh);
            foreach (var material in materials) if (material != null) Destroy(material);
        }
    }
}
