using UnityEngine;
namespace HumanMotion.ControlStudio
{
    // Bounded, surface-local virtual fastener marks. No per-impact GameObject allocation.
    public sealed class Tool1FastenableSurface : MonoBehaviour
    {
        public const int Capacity=192;
        public int MarkCount {get;private set;}
        public int Generation {get;private set;}
        readonly Vector3[] vertices=new Vector3[Capacity*8];
        readonly Vector3[] centers=new Vector3[Capacity];
        readonly int[] indices=new int[Capacity*12];
        Mesh mesh;Material material;int cursor;
        public void Initialize(Material markMaterial)
        {
            material=new Material(markMaterial);
            var child=new GameObject("Bounded virtual fastener marks");child.transform.SetParent(transform,false);
            mesh=new Mesh{name="Virtual fastener crosses (192 max)"};mesh.MarkDynamic();
            child.AddComponent<MeshFilter>().sharedMesh=mesh;child.AddComponent<MeshRenderer>().sharedMaterial=material;
            for(int i=0;i<Capacity;i++){
                int v=i*8,k=i*12;
                indices[k]=v;indices[k+1]=v+1;indices[k+2]=v+2;indices[k+3]=v;indices[k+4]=v+2;indices[k+5]=v+3;
                indices[k+6]=v+4;indices[k+7]=v+5;indices[k+8]=v+6;indices[k+9]=v+4;indices[k+10]=v+6;indices[k+11]=v+7;
            }
            Clear();
        }
        public void Clear(){MarkCount=cursor=0;Generation++;System.Array.Clear(vertices,0,vertices.Length);Upload();}
        public bool TryMark(Vector3 worldPoint,Vector3 normal,float size)
        {
            if(mesh==null)return false;
            var center=transform.InverseTransformPoint(worldPoint+normal*.0012f);
            float scale=Mathf.Max(.0001f,transform.lossyScale.x);
            float localSpacing=.014f/scale;
            for(int i=0;i<MarkCount;i++)if(Vector3.Distance(center,centers[i])<localSpacing)return false;
            centers[cursor]=center;
            float half=Mathf.Clamp(size,.005f,.018f)/scale*.5f,thin=half*.22f;
            int v=cursor*8;
            vertices[v]=center+new Vector3(-half,-thin,0);vertices[v+1]=center+new Vector3(half,-thin,0);
            vertices[v+2]=center+new Vector3(half,thin,0);vertices[v+3]=center+new Vector3(-half,thin,0);
            vertices[v+4]=center+new Vector3(-thin,-half,0);vertices[v+5]=center+new Vector3(thin,-half,0);
            vertices[v+6]=center+new Vector3(thin,half,0);vertices[v+7]=center+new Vector3(-thin,half,0);
            cursor=(cursor+1)%Capacity;MarkCount=Mathf.Min(Capacity,MarkCount+1);Upload();return true;
        }
        void Upload(){if(mesh==null)return;mesh.vertices=vertices;mesh.triangles=indices;mesh.RecalculateBounds();}
        void OnDestroy(){if(mesh!=null)Destroy(mesh);if(material!=null)Destroy(material);}
    }
}
