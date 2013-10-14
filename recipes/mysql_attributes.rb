#
# Cookbook Name:: common
# Recipe:: mysql_attributes
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

node.override['mysql']['tunable']['server_id'] = node['ipaddress'][/\d+\.\d+$/].gsub(/\./, '')
