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
# timestore.py
# Python module for accessing a Timestore database
#

import httplib
import urllib, urlparse
import time
import json
import hmac, hashlib, base64
from datetime import datetime, timedelta
from base64 import b64encode, b64decode

def print_timing(func):
	def wrapper(*args, **kwargs):
		t1 = time.time()
		res = func(*args, **kwargs)
		t2 = time.time()
		print "%s took %0.3f ms" % (func.func_name, (t2-t1)*1000.0)
		return res
	return wrapper

class TimestoreException(Exception):
	def __init__(self, message, status):
		Exception.__init__(self, message)
		self.status = status

class Client(object):
	def __init__(self, host = '127.0.0.1:8080'):
		self.conn = httplib.HTTPConnection(host, strict = True)

#	@print_timing		
	def __do_request(self, method, path, req = None, args = None, key = None, redirect = True):
		self.conn.connect() # Will this persist if already connected (tested - NO)

		# JSON encode request body		
		req_body = None
		if req:
			req_body = json.dumps(req)

		# If a key is provided then sign the request
		headers = {}
		if key:
			msg = "%s\n%s\n" % (method, path)
			if args:
				for n,v in args.items():
					msg = msg + "%s=%s\n" % (n,v)
			
			if req_body:
				msg = msg + req_body
			mac = hmac.new(key, msg, hashlib.sha256)
			headers['Signature'] = base64.b64encode(mac.digest())
			
		# Add query string
		if args:
			path = path + '?' + urllib.urlencode(args)

		self.conn.request(method, path, req_body, headers)
		r = self.conn.getresponse()
		resp_body = r.read()
		# Handle error responses
		if r.status >= 400:
			raise TimestoreException("HTTP error %d %s" % (r.status, r.reason), r.status)
		# Handle redirects
		location = r.getheader('Location')
		if redirect and location:
			o = urlparse.urlparse(location)
#			print "Redirecting to", o.path
			return self.__do_request('GET', o.path, req, args, key, redirect)
		if resp_body and len(resp_body) > 0:
			resp = json.loads(resp_body)
		else:
			resp = None

		# Some versions of Python are really slow if we use
		# persistent connections
		self.conn.close()
		return (r.status, resp)

	def get_nodes(self, key = None):
		(status, nodes) = self.__do_request('GET', '/nodes', key = key)
		return nodes
			
	def get_node(self, node_id, key = None):
		(status, resp) = self.__do_request('GET', "/nodes/%x" % (node_id), key = key)
		return resp

	def create_node(self, node_id, metadata, key = None):
		(status, resp) = self.__do_request('PUT', "/nodes/%x" % (node_id), metadata, key = key)

	def delete_node(self, node_id, key = None):
		(status, resp) = self.__do_request('DELETE', "/nodes/%x" % (node_id), key = key)

	def get_keys(self, node_id, key = None):
		(status, resp) = self.__do_request('GET', "/nodes/%x/keys" % (node_id), key = key)
		return resp

	def get_key(self, node_id, key_name, key = None):
		(status, resp) = self.__do_request('GET', "/nodes/%x/keys/%s" % (node_id, key_name), key = key)
		if not len(resp['key']):
			return None
		else:
			return b64decode(resp['key'])

	def set_key(self, node_id, key_name, keyval, key = None):
		req = {}
		if keyval:
			req['key'] = b64encode(keyval)
		else:
			req['key'] = '' # Blank string clears key
		(status, resp) = self.__do_request('PUT', "/nodes/%x/keys/%s" % (node_id, key_name), req, key = key)

	def get_values(self, node_id, timestamp = None, key = None):
		if timestamp:
			unixtime = time.mktime(datetime.timetuple(timestamp))
			(status, resp) = self.__do_request('GET', "/nodes/%x/values/%d" % (node_id, unixtime), key = key)
		else:
			# Get latest - server will redirect us
			(status, resp) = self.__do_request('GET', "/nodes/%x/values" % (node_id), key = key)
		# Convert returned timestamp from JS UNIX time into Python datetime
		ts = datetime.fromtimestamp(resp['timestamp'] / 1000.0)
		return (ts, resp['values'])
		
	def submit_values(self, node_id, values, timestamp = None, key = None):
		req = { 'values' : values }
		if timestamp:
			# Convert timestamp from datetime to UNIX timestamp in ms
			jstime = time.mktime(datetime.timetuple(timestamp)) * 1000.0
			req['timestamp'] = jstime
		# Submit without following the returned redirection - it's a waste of time
		(status, resp) = self.__do_request('POST', "/nodes/%x/values" % (node_id), req, key = key, redirect = False)
		return (status, resp)
		# Convert returned timestamp from JS UNIX time into Python datetime
#		ts = datetime.fromtimestamp(resp['timestamp'] / 1000.0)
#		return (ts, resp['values'])
		
	def get_series(self, node_id, metric_id, npoints, start = None, end = None, key = None):
		url = "/nodes/%x/series/%x" % (node_id, metric_id)
		args = { 'npoints' : npoints }
		
		# Convert start/end to UNIX timestamp
		if start:
			args['start'] = time.mktime(datetime.timetuple(start))
		if end:
			args['end'] = time.mktime(datetime.timetuple(end))
		(status, series) = self.__do_request('GET', url, args = args, key = key)
		return series
	
