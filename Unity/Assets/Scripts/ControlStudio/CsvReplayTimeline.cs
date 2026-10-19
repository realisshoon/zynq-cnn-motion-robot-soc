using System;
namespace HumanMotion.ControlStudio
{
    // Input event first at exact ties; native output ticks stay on n*0.020 seconds.
    // Pure scheduler: no render FPS, wall clock, Unity Transform, or calibration math.
    public sealed class CsvReplayTimeline
    {
        readonly RecordedHumanCsv data;
        readonly Action<RecordedHumanRow> consume;
        readonly Func<bool> tick;
        readonly Func<bool> settled;
        long nextTick=1;
        public int Index {get;private set;}=-1;
        public double Time {get;private set;}
        public bool Finished {get;private set;}
        public bool EndOfInput=>Index==data.Rows.Count-1;
        public CsvReplayTimeline(RecordedHumanCsv data,Action<RecordedHumanRow> consume,Func<bool> tick,Func<bool> settled)
        {this.data=data;this.consume=consume;this.tick=tick;this.settled=settled;}
        public void Advance(double dt)
        {
            if(dt<0||double.IsNaN(dt)||double.IsInfinity(dt))throw new ArgumentOutOfRangeException(nameof(dt));
            if(Finished)return;double until=Time+dt;
            while(true)
            {
                double input=EndOfInput?double.PositiveInfinity:data.Rows[Index+1].Time;
                double output=nextTick*.020;
                if(input<=output+1e-9&&input<=until+1e-9){Time=input;Index++;consume(data.Rows[Index]);}
                else if(output<=until+1e-9){Time=output;nextTick++;if(!tick())return;if(EndOfInput&&settled()){Finished=true;return;}}
                else break;
            }
            Time=until;
        }
        public void Step(){if(Finished)return;Advance(EndOfInput?Math.Max(0,nextTick*.020-Time):Math.Max(0,data.Rows[Index+1].Time-Time));}
    }
}
