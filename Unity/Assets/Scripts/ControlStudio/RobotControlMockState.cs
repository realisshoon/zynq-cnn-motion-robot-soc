namespace HumanMotion.ControlStudio
{
    // UI-only state. No transport, servo command or source ownership.
    public sealed class RobotControlMockState
    {
        public bool Pwm {get;private set;}
        public bool Follow {get;private set;}
        public int Red=40,Green=30,Blue=50;
        public int AppliedRed {get;private set;}=40;
        public int AppliedGreen {get;private set;}=30;
        public int AppliedBlue {get;private set;}=50;
        public int ApplyCount {get;private set;}
        public string Message {get;private set;}="MOCK ONLY · 실제 송신 없음";
        public void EnablePwm(){Pwm=true;Message="MOCK · PWM ON";}
        public bool StartFollow(){if(!Pwm){Message="PWM ON 후 추종 시작 가능";return false;}Follow=true;Message="MOCK · FOLLOW ON";return true;}
        public void StopFollow(){Follow=false;Message="MOCK · FOLLOW OFF / PWM 유지";}
        public void DisablePwm(){Pwm=false;Follow=false;Message="MOCK · PWM OFF / FOLLOW OFF";}
        public void Refresh(){Message="MOCK 상태 확인 · 장치 조회 없음";}
        public void Apply(){AppliedRed=Red;AppliedGreen=Green;AppliedBlue=Blue;ApplyCount++;Message=$"MOCK 적용 #{ApplyCount} · 송신 없음";}
    }
}
