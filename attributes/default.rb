#
# Cookbook Name:: common
# Attribute:: default
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

# for apache recipe
default['common']['apache']['allow_from'] = '127.0.0.1'

# for activemq recipe
default['activemq']['policy_entry']['topic']['memory_limit'] = '1mb'
default['activemq']['system_usage']['memory_usage_limit'] = '64 mb'
default['activemq']['system_usage']['store_usage_limit'] = '100 gb'
default['activemq']['system_usage']['temp_usage_limit'] = '50 gb'
