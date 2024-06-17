The dwUMrun.sh script will start serveral back ground tasks.  They will start and stop queries jobs at
designated times.  This script must be executed on the UM.

The dwPMrun.sh script will start cpimport jobs at designated time.  It must be executed on
the active PM.

To prevent the next job from starting (to cancel testing), echo a 0 to the follow file
/home/qa/srv/autopilot/stability/dwweek/data/continue.txt

echo 0 > /home/qa/srv/autopilot/stability/dwweek/data/continue.txt

