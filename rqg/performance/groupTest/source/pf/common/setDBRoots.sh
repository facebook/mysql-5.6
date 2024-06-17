#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    setDBRoots.sh
#*  Date Created:   2008.11.21
#*  Author:         Stephen Cargile
#*  Purpose:        Set the proper device names in Calpont.xml and then unmount & mount
#*                   the devices to the appropriate mount points
#*
#*  Parameters:	    setNum - 1 or 2:  dbroot set number
#*                  
#******************************************************************************/
#
#-----------------------------------------------------------------------------
# Get dbroot set number from user input (command line parameter)
#-----------------------------------------------------------------------------
setNum=$1
echo *****-----*****-----*****-----*****-----*****
echo Start - Set dbroots to RAID Configuration $setNum
echo *****-----*****-----*****-----*****-----*****
#-----------------------------------------------------------------------------
# unmount dbroots from all PMs
#-----------------------------------------------------------------------------
echo unmounting PM1
/usr/local/Calpont/bin/remote_command.sh srvqaperf3 qalpont! "umount -a"
echo unmounting PM2
/usr/local/Calpont/bin/remote_command.sh srvqaperf4 qalpont! "umount -a"
echo unmounting PM3
/usr/local/Calpont/bin/remote_command.sh srvqaperf5 qalpont! "umount -a"
echo unmounting PM4
/usr/local/Calpont/bin/remote_command.sh srvqaperf8 qalpont! "umount -a"
#-----------------------------------------------------------------------------
# save current fstab to fstab.auto then move 'set number' fstab to 'real' fstab
#-----------------------------------------------------------------------------
echo *-*
echo doing the hokey pokey with fstabs on  PM1
/usr/local/Calpont/bin/remote_command.sh srvqaperf3 qalpont! "rm -f /etc/fstab.auto"
/usr/local/Calpont/bin/remote_command.sh srvqaperf3 qalpont! "mv /etc/fstab /etc/fstab.auto"
/usr/local/Calpont/bin/remote_command.sh srvqaperf3 qalpont! "cp /etc/fstab.[$setNum] /etc/fstab"
echo *-*
echo doing the funky chicken with fstabs on  PM2
/usr/local/Calpont/bin/remote_command.sh srvqaperf4 qalpont! "rm -f /etc/fstab.auto"
/usr/local/Calpont/bin/remote_command.sh srvqaperf4 qalpont! "mv /etc/fstab /etc/fstab.auto"
/usr/local/Calpont/bin/remote_command.sh srvqaperf4 qalpont! "cp /etc/fstab.[$setNum] /etc/fstab"
echo *-*
echo doing the swim with fstabs on  PM3
/usr/local/Calpont/bin/remote_command.sh srvqaperf5 qalpont! "rm -f /etc/fstab.auto"
/usr/local/Calpont/bin/remote_command.sh srvqaperf5 qalpont! "mv /etc/fstab /etc/fstab.auto"
/usr/local/Calpont/bin/remote_command.sh srvqaperf5 qalpont! "cp /etc/fstab.[$setNum] /etc/fstab"
echo *-*
echo doing the stroll with fstabs on  PM4
/usr/local/Calpont/bin/remote_command.sh srvqaperf8 qalpont! "rm -f /etc/fstab.auto"
/usr/local/Calpont/bin/remote_command.sh srvqaperf8 qalpont! "mv /etc/fstab /etc/fstab.auto"
/usr/local/Calpont/bin/remote_command.sh srvqaperf8 qalpont! "cp /etc/fstab.[$setNum] /etc/fstab"
#-----------------------------------------------------------------------------
# re-mount dbroots on all PMs
#-----------------------------------------------------------------------------
echo *-*
echo mounting PM1
/usr/local/Calpont/bin/remote_command.sh srvqaperf3 qalpont! "mount -a"
echo mounting PM2
/usr/local/Calpont/bin/remote_command.sh srvqaperf4 qalpont! "mount -a"
echo mounting PM3
/usr/local/Calpont/bin/remote_command.sh srvqaperf5 qalpont! "mount -a"
echo mounting PM4
/usr/local/Calpont/bin/remote_command.sh srvqaperf8 qalpont! "mount -a"
#-----------------------------------------------------------------------------
echo
#-----------------------------------------------------------------------------
echo set disk scheduler to deadline for newly mounted LUNs
#-----------------------------------------------------------------------------
echo
echo setting disk scheduler to deadline on PM1
/usr/local/Calpont/bin/remote_command.sh srvqaperf3 qalpont! "/etc/rc.d/rc.local"
echo setting disk scheduler to deadline on PM2
/usr/local/Calpont/bin/remote_command.sh srvqaperf4 qalpont! "/etc/rc.d/rc.local"
echo setting disk scheduler to deadline on PM3
/usr/local/Calpont/bin/remote_command.sh srvqaperf5 qalpont! "/etc/rc.d/rc.local"
echo setting disk scheduler to deadline on PM4
/usr/local/Calpont/bin/remote_command.sh srvqaperf8 qalpont! "/etc/rc.d/rc.local"
#
echo -----*****-----*****-----*****-----*****-----**
echo End - set dbroots to RAID Configuration $setNum
echo -----*****-----*****-----*****-----*****-----**
# End of script
