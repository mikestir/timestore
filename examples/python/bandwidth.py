#!/usr/bin/env python
#
# File-based time series database
#
# Copyright (C) 2012, 2013 Mike Stirling
#
# This file is part of TimeStore (http://www.livesense.co.uk/timestore)
#
# All rights reserved.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# bandwidth.py
# Example of using Python to poll for network interface load and
# submitting the result to Timestore
#

import time
import daemon
from timestore import Client, TimestoreException

# Interface to monitor
IFACE = 'eth0'
# Update rate (seconds)
RATE = 10
# Database node number
NODE = 0x999
# Database host/port
DB = 'localhost:8080'
# Update key - specify here if a key defined using tsadmin
#WRITE_KEY = '9' * 32
WRITE_KEY = None

# Returns current byte totals for rx and tx from the selected
# network interface
def get_current(iface):
	# Parse out required interface
	f = open('/proc/net/dev', 'r')
	all = f.readlines();
	f.close()

	for dev in all:
		dev = dev.strip().split()
		if dev[0] == iface + ':':
			# Return current throughput bytes for rx/tx
			return (int(dev[1]), int(dev[9]))

	return None

# Daemonise
context = daemon.DaemonContext()

print "Starting daemon"
with context:
	# Connect to timestore
	t = Client(DB)

	(rxlast, txlast) = (None, None)
	while 1:
		(rx, tx) = get_current(IFACE)
		if rxlast and txlast:
			# Calculate bandwidth and push to database
			bwrx = (rx - rxlast) * 8 / RATE
			bwtx = (tx - txlast) * 8 / RATE
			t.submit_values(NODE, [bwrx / 1.0e3, bwtx / 1.0e3], key = WRITE_KEY)
	
		(rxlast, txlast) = (rx, tx)

		# Sleep until next interval
		time.sleep(RATE)
