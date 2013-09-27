#
# Cookbook Name:: common
# Definition:: postfix
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_postfix_postmap, :content => "", :owner => "root", :group => "root", :mode => 0644 do
  execute "postmap-#{params[:name]}" do
    command "postmap /etc/postfix/#{params[:name]}"
    action :nothing
    notifies :restart, "service[postfix]"
  end

  template "/etc/postfix/#{params[:name]}" do
    source "template.erb"
    variables({:params => params})
    owner params[:owner]
    group params[:group]
    mode params[:mode]
    action :create
    notifies :run, "execute[postmap-#{params[:name]}]"
  end
end
