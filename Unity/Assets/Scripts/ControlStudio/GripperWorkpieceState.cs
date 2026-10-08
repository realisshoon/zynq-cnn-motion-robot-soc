using UnityEngine;

namespace HumanMotion.ControlStudio
{
    public enum WorkpieceOwner { None, LeftFeeder, RightProcess, Completed, Environment }
    public enum WorkpieceStage { AvailableAtSupply, HeldByLeft, AvailableAtPickup, HeldByRight, PlacedAtTarget }

    // A single persistent box is reparented between fixed cell and visual carry anchors.
    public sealed class GripperWorkpieceState : MonoBehaviour
    {
        public int Id {get;private set;}
        public WorkpieceOwner Owner {get;private set;}
        public WorkpieceStage Stage {get;private set;}
        public Rigidbody Body {get;private set;}
        public BoxCollider Collider {get;private set;}
        public void Initialize(int id)
        {
            Id=id;name=$"BOX_{id:000}";Owner=WorkpieceOwner.None;
            Body=GetComponent<Rigidbody>();Collider=GetComponent<BoxCollider>();
            Body.isKinematic=true;Body.useGravity=false;Body.interpolation=RigidbodyInterpolation.Interpolate;
        }
        public void InitializeAtSupply(int id)
        {
            Initialize(id);
            name=$"WorkpieceBox_{id:000}";
            Owner=WorkpieceOwner.Environment;
            Stage=WorkpieceStage.AvailableAtSupply;
        }
        public bool Attach(WorkpieceOwner arm,Transform carry)
        {
            if((Owner!=WorkpieceOwner.None&&Owner!=WorkpieceOwner.Environment)||carry==null||arm==WorkpieceOwner.None||arm==WorkpieceOwner.Environment||arm==WorkpieceOwner.Completed)return false;
            transform.SetParent(carry,true);Owner=arm;Body.linearVelocity=Vector3.zero;Body.angularVelocity=Vector3.zero;return true;
        }
        public bool Release(WorkpieceOwner arm,Transform cellRoot)
        {
            if(Owner!=arm||cellRoot==null)return false;
            transform.SetParent(cellRoot,true);Owner=WorkpieceOwner.None;Body.linearVelocity=Vector3.zero;Body.angularVelocity=Vector3.zero;return true;
        }
        public void Complete(){if(Owner==WorkpieceOwner.None)Owner=WorkpieceOwner.Completed;}
    }
}
