<?php

  /*
     Timestore php wrapper is released under the GNU Affero General Public License.
     See LICENSE for full text
     php wrapper author: Trystan Lea https://github.com/TrystanLea
     timestore: Mike Stirling http://www.livesense.co.uk/timestore
  */

  error_reporting(E_ALL);      
  ini_set('display_errors', 'on'); 

  // Insert your admin key here, you can find it here: /var/lib/timestore/adminkey.txt
  // The admin key is generated when timestore is started
  $adminkey = 'wOY7,]@WLCcf73V?vH=^ca9jSm09]gvW';
 
  // User key's provide a way to authenticate access to specific nodes
  // Generate a new key:
  // echo md5(uniqid(mt_rand(), true));
  $user_read_key = '6c987aa5b6ffa80eecc2c737fefc5d72';
  $user_write_key = 'afb6ffsdfa80eecc2fsdga37fefsgsgd';

  require "timestore_class.php";
  $timestore = new Timestore($adminkey);

  // 1) Start by creating a node
  print $timestore->create_node(1,10);

  // 2) Set the node user key (optional)
  // a node can be public without a user key
  print $timestore->set_key(1,'write',$user_write_key);
  print $timestore->set_key(1,'read',$user_read_key);

  // To read a node key use:
  // print $timestore->get_key(1,'read');

  // --------------------------------------------------

  // With the node created and key set comment out lines 26,30 and 31 above
  // and uncomment the following:

  // 1) Post some values into the node:
  // print $timestore->post_values(1,time()*1000,array(1280),$user_write_key);

  // 2) Fetch series from node:
  // print $timestore->get_series(1,0,1000,time()-10000,time(),$user_read_key);

  // Not yet implemented in timestore
  // print $timestore->get_nodes(key);
