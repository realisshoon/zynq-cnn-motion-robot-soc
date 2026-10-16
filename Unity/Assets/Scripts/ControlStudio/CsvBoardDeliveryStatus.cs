namespace HumanMotion.ControlStudio
{
    // Observation only. Current USB-console firmware has no PC final-joint ingress
    // contract/ACK. Neither TK telemetry nor UART0 board-to-board human targets
    // authorize host CSV streaming. Do not probe with invented command bytes.
    public static class CsvBoardDeliveryStatus
    {
        public static string Describe(BoardUartSource boards,int selectedTarget)
        {
            if(boards==null)return "FPGA: 보드 미연결 · Unity 재생 가능";
            if(selectedTarget==2)return Side(boards,BoardSide.Right)+"\n"+Side(boards,BoardSide.Left);
            return Side(boards,selectedTarget==1?BoardSide.Left:BoardSide.Right);
        }
        static string Side(BoardUartSource boards,BoardSide side)
        {
            string name=side==BoardSide.Left?"LEFT / Robot1":"RIGHT / Robot0";
            var s=boards.Session(side);
            if(!s.Connected)return name+": 보드 미연결 · Unity 재생 가능";
            if(boards.IsFixture(side))return name+": RX Fixture · 보드 CSV 입력 미지원 · TX 없음";
            if(!boards.IsPortOpen(side))return name+": 연결 대기/오류 · CSV 송신 없음";
            return name+": 보드 CSV 입력 미지원 · 송신 없음"+
                (s.Trusted?"\n보드 승인: 없음 / PWM 적용: 미확인":"\n보드 역할/상태 미확인 · 자동 상태 조회 없음");
        }
    }
}
