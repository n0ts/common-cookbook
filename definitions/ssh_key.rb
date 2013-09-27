#
# Cookbook Name:: common
# Definition:: ssh_key
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_ssh_keys, :id_rsa => "", :id_rsa_pub => "", :ssh_config => "" do
  directory "/home/#{params[:name]}/.ssh" do
    owner params[:name]
    group params[:name]
    mode 0700
    action :create
  end

  file "/home/#{params[:name]}/.ssh/config" do
    content params[:ssh_config]
    owner params[:name]
    group params[:name]
    mode 0600
    action :create
    not_if { params[:ssh_config].empty? }
  end

  file "/home/#{params[:name]}/.ssh/id_rsa" do
    content params[:id_rsa]
    owner params[:name]
    group params[:name]
    mode 0400
    action :create
  end

  file "/home/#{params[:name]}/.ssh/id_rsa.pub" do
    content params[:id_rsa_pub]
    owner params[:name]
    group params[:name]
    mode 0644
    action :create
    not_if { params[:id_rsa_pub].empty? }
  end
end

define :common_ssh_keys_private_key, :key_file => "", :id_rsa => "" do
  file "/home/#{params[:name]}/.ssh/#{params[:key_file]}" do
    content params[:id_rsa]
    owner params[:name]
    group params[:name]
    mode 0400
    action :create
  end
end

define :common_ssh_keys_config, :ssh_config => {} do
  template "/home/#{params[:name]}/.ssh/config" do
    source "ssh_config.erb"
    cookbook "common"
    owner params[:name]
    group params[:name]
    variables({ :ssh_config => params[:ssh_config] })
    mode 0600
    action :create
  end
end
