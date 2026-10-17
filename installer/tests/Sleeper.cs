// Stand-in for venetium.exe in the installer test. A running .NET exe keeps its own image file locked, so when it is
// copied over the installed venetium.exe and started, Setup's "is Venetium running from here?" check fires (the lock
// probe and the process scan). It is never the real browser; the test starts it and kills it.
using System.Threading;

internal static class Sleeper
{
    private static void Main()
    {
        Thread.Sleep(600000);   // 10 min; the test kills it well before this
    }
}
