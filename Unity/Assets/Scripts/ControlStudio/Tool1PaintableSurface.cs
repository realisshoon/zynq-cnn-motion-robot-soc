using UnityEngine;
namespace HumanMotion.ControlStudio
{
    public sealed class Tool1PaintableSurface : MonoBehaviour
    {
        const int Size=256;
        Texture2D texture;Material owned;Color32[] pixels;
        public int PaintCount {get;private set;}
        public void Initialize(Material template)
        {
            owned=new Material(template);GetComponent<Renderer>().sharedMaterial=owned;
            texture=new Texture2D(Size,Size,TextureFormat.RGBA32,false);texture.wrapMode=TextureWrapMode.Clamp;
            pixels=new Color32[Size*Size];owned.mainTexture=texture;Clear();
        }
        public void Clear(){if(pixels==null)return;for(int i=0;i<pixels.Length;i++)pixels[i]=Color.white;PaintCount=0;Upload();}
        public void Paint(Vector2 uv,Color color,float worldRadius,float seconds)
        {
            if(texture==null||seconds<=0)return;int radius=Mathf.Clamp(Mathf.CeilToInt(worldRadius/transform.lossyScale.x*Size),1,Size/3);
            int cx=Mathf.RoundToInt(uv.x*(Size-1)),cy=Mathf.RoundToInt(uv.y*(Size-1));
            for(int y=Mathf.Max(0,cy-radius);y<=Mathf.Min(Size-1,cy+radius);y++)for(int x=Mathf.Max(0,cx-radius);x<=Mathf.Min(Size-1,cx+radius);x++){
                float d=new Vector2(x-cx,y-cy).magnitude/radius;if(d<=1)pixels[y*Size+x]=Color.Lerp(pixels[y*Size+x],color,Mathf.Clamp01(seconds*14*(1-d)));
            }
            PaintCount++;Upload();
        }
        void Upload(){texture.SetPixels32(pixels);texture.Apply(false,false);}
        void OnDestroy(){if(owned!=null)Destroy(owned);if(texture!=null)Destroy(texture);}
    }
}
