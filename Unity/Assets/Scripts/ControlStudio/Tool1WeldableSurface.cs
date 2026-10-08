using UnityEngine;
namespace HumanMotion.ControlStudio
{
    // Bounded, surface-local bead mesh. No per-sample GameObject allocation.
    public sealed class Tool1WeldableSurface : MonoBehaviour
    {
        public const int Capacity=512;
        public int MarkCount {get;private set;}
        public int Generation {get;private set;}
        public float LongestSegment {get;private set;}
        public int StrokeCount {get;private set;}
        Mesh mesh;Material material;int cursor;
        readonly Vector3[] vertices=new Vector3[Capacity*4];
        readonly int[] indices=new int[Capacity*6];
        public void Initialize(Material beadMaterial)
        {
            material=new Material(beadMaterial);var child=new GameObject("Bounded weld beads");child.transform.SetParent(transform,false);
            mesh=new Mesh{name="Virtual weld marks (512 quads max)"};mesh.MarkDynamic();child.AddComponent<MeshFilter>().sharedMesh=mesh;child.AddComponent<MeshRenderer>().sharedMaterial=material;
            for(int i=0;i<Capacity;i++){int v=i*4,k=i*6;indices[k]=v;indices[k+1]=v+1;indices[k+2]=v+2;indices[k+3]=v;indices[k+4]=v+2;indices[k+5]=v+3;}
            Clear();
        }
        public void Clear(){MarkCount=cursor=StrokeCount=0;LongestSegment=0;Generation++;System.Array.Clear(vertices,0,vertices.Length);Upload();}
        public void Mark(Vector3 from,Vector3 to,Vector3 normal,float width,bool start)
        {
            if(mesh==null)return;float length=Vector3.Distance(from,to);LongestSegment=Mathf.Max(LongestSegment,length);if(start)StrokeCount++;
            Vector3 tangent=length>.00001f?(to-from).normalized:transform.right;
            Vector3 side=Vector3.Cross(normal,tangent).normalized*width*.5f;
            if(start){from=to-tangent*width*.5f;to+=tangent*width*.5f;}
            Vector3 lift=normal*.0007f;int v=cursor*4;
            vertices[v]=transform.InverseTransformPoint(from-side+lift);vertices[v+1]=transform.InverseTransformPoint(from+side+lift);
            vertices[v+2]=transform.InverseTransformPoint(to+side+lift);vertices[v+3]=transform.InverseTransformPoint(to-side+lift);
            cursor=(cursor+1)%Capacity;MarkCount=Mathf.Min(Capacity,MarkCount+1);Upload();
        }
        void Upload(){if(mesh==null)return;mesh.vertices=vertices;mesh.triangles=indices;mesh.RecalculateBounds();}
        void OnDestroy(){if(mesh!=null)Destroy(mesh);if(material!=null)Destroy(material);}
    }
}
