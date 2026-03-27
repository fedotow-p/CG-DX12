//
// Created by elm on 27.03.2026.
//

#ifndef CG_DX12_TIMER_H
#define CG_DX12_TIMER_H

class Timer {
    public:
    Timer();

    float TotalTime() const;
    float DeltaTime() const;

    void Reset();
    void Start();
    void Stop();
    void Tick();

    private:
    double mSecondsPerCount;
    double mDeltaTime;

    __int64 mBaseTime;
    __int64 mPausedTime;
    __int64 mStopTime;
    __int64 mPrevTime;
    __int64 mCurrTime;

    bool mStopped;
};

#endif //CG_DX12_TIMER_H
