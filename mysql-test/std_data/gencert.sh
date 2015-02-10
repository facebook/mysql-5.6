#!/bin/sh -eu

# A very simple proto-script for generating a new CA, and using it to
# sign the server and client certificates.

CADIR=/tmp/ca
rm -rf $CADIR
mkdir $CADIR
> $CADIR/index.txt
echo "01" > $CADIR/serial
cp /etc/pki/tls/openssl.cnf $CADIR
replace /etc/pki/CA $CADIR -- $CADIR/openssl.cnf


openssl genrsa 2048 > $CADIR/cakey.pem
openssl req -new -x509 -nodes -days 3600 -key $CADIR/cakey.pem -out cacert.pem -subj '/C=SE/ST=Uppsala/L=Uppsala/O=MySQL AB'

openssl req -newkey rsa:2048 -days 3600 -nodes -keyout server-key.pem -out $CADIR/server-req.pem -subj '/C=SE/ST=Uppsala/O=MySQL AB/CN=localhost'
openssl ca -batch -config $CADIR/openssl.cnf -days 3650 -policy policy_anything -cert cacert.pem -keyfile $CADIR/cakey.pem -outdir $CADIR -out server-cert.pem -infiles $CADIR/server-req.pem

openssl req -newkey rsa:2048 -days 3600 -nodes -keyout client-key.pem -out $CADIR/client-req.pem -subj '/C=SE/ST=Uppsala/O=MySQL AB/CN=client'
openssl ca -batch -config $CADIR/openssl.cnf -days 3650 -policy policy_anything -cert cacert.pem -keyfile $CADIR/cakey.pem -outdir $CADIR -out client-cert.pem -infiles $CADIR/client-req.pem
