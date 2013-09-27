#
# Cookbook Name:: common
# Definition:: capistrano
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_capistrano, :ruby_version => "" do
  directory "/usr/local/etc/capistrano_v3" do
    owner "root"
    group "root"
    mode 0755
    action :create
  end

  file "/usr/local/etc/capistrano_v3/Gemfile" do
    content <<-EOH
source 'https://rubygems.org'

gem 'capistrano', github: 'capistrano/capistrano', branch: 'v3'
gem 'capistrano-rails'
EOH
    owner "root"
    group "root"
    mode 0644
    action :create
    notifies :run, "bash[install_capistrano_v3]"
  end

  bash "install_capistrano_v3" do
    user "root"
    group "root"
    cwd "/usr/local/etc/capistrano_v3"
    code <<-EOH
      bundle --binstubs
EOH
    action :nothing
  end

  file "/etc/profile.d/capistrano_v3.sh" do
    owner "root"
    group "root"
    mode 0755
    content <<-EOH
export PATH=/usr/local/etc/capistrano_v3/bin:$PATH
EOH
  end
end
