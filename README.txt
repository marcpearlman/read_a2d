########################################################################
2018-02-10

read_a2d periodically reads digitized values from an A2D channel 
on the TI AM335x evaluation module (EVM) and reports to them to
a remote server

Usage: ./read_a2d

Notes:
* Program for the remote server is pending.  The remote server
  simply receives the data reported from the sensing node and
  either records it into a flat logfile or to a MySQL database.
* All parameters are currently hard coded.  It is planned that
  the following will be configurable via the command line:
  - a2d sampling interval                (default value 500ms)
  - messaging interval to remote server  (default: 2.0 sec)
  - remote server IP address             (default: 192.168.1.100)
  - remote server port                   (default: 5231)
* Additional features can be made available upon request.
