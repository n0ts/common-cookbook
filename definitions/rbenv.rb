#
# Cookbook Name:: common
# Definition:: rbenv
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_rbenv, :gems => [ "bundler" ] do
  rbenv_ruby params[:name] do
    global true
    action :install
  end

  params[:gems].each do |gem|
    rbenv_gem gem do
      ruby_version params[:name]
    end
  end

  git "#{node[:rbenv][:install_prefix]}/rbenv/plugins/rbenv-sudo" do
    repository "git://github.com/dcarley/rbenv-sudo.git"
    reference "master"
    action :sync
  end
end
