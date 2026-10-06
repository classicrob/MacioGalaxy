#!/bin/bash
# Lead: choose who takes a lock next.  usage: build/lock-grant.sh <build|app|sweep-lane> <owner-or-ticket-id>
case $1 in build) L=build/.macos-gx.lock;; app) L=build/.petari-app.lock;; sweep-lane) L=build/.petari-sweep-lane.lock;; *) echo "lock?"; exit 2;; esac
echo "$2" > $L.grant; echo "granted $1 -> $2"
