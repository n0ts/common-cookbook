#
# Cookbook Name:: common
# Definition:: directory
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_directory, :owner => "", :group => "" do
  directory "/#{params[:name]}" do
    owner params[:owner]
    group params[:group]
    mode 0755
    action :create
  end

  %w{ bin etc var }.each do |dir|
    directory "/#{params[:name]}/#{dir}" do
      owner params[:owner]
      group params[:group]
      mode 0755
      action :create
    end
  end

  %w{ log run tmp }.each do |dir|
    directory "/#{params[:name]}/var/#{dir}" do
      owner params[:owner]
      group params[:group]
      mode 0775
      action :create
    end
  end
end
