#
# Cookbook Name:: common
# Recipe:: nginx_ltsv
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

log_format_main =<<EOS
'$remote_addr - $remote_user [$time_local] "$request" '
                    '$status $body_bytes_sent "$http_referer" '
                    '"$http_user_agent" "$http_x_forwarded_for"'
EOS

log_format_ltsv =<<EOS
'ts:$time_iso8601\\t'
                    'host:$host\\t'
                    'ip:$remote_addr\\t'
                    'method:$request_method\\t'
                    'path:$request_uri\\t'
                    'status:$status\\t'
                    'size_req:$request_length\\t'
                    'size_res:$bytes_sent\t'
                    'size_body:$body_bytes_sent\\t'
                    'time_req:$request_time\\t'
                    'time_app:$upstream_response_time\\t'
                    'referer:$http_referer\\t'
                    'ua:$http_user_agent\\t'
EOS

node.override = {
  'log_format' => {
    :main => log_format_main.chomp,
    :ltsv => log_format_ltsv.chomp,
  },
  'access_log_options' => "ltsv",
}
