#
# Cookbook Name:: common
# Definition:: logrotate
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_logrote, :type => "daily", :log_dir => "", :filename => "", :logrotate_rotate => 30 do
  case params[:type]
  when "hourly"
    logrotate_exclude_filename = "*.`date +%Y-%m-%d-%H`*"
  when "daily"
    logrotate_exclude_filename = "*.`date +%Y-%m-%d`*"
  else
    return
  end

  file "/var/log/dummy_#{params[:name]}" do
    content ""
    owner "root"
    group "root"
    mode 0644
    action :create
  end

  prearchive_dir = "#{params[:log_dir]}/prearchive"
  directory prearchive_dir do
    owner "root"
    group "root"
    mode 0755
    action :create
  end

  archive_dir = "#{params[:log_dir]}/archive"
  template "/etc/logrotate.d/#{params[:name]}" do
    cookbook "common"
    source "log-logrotate.erb"
    variables :logrotate_dummy            => "dummy_#{params[:name]}",
              :logrotate_exclude_filename => logrotate_exclude_filename,
              :logrotate_filename         => params[:filename],
              :logrotate_rotate           => params[:logrotate_rotate],
              :log_dir                    => params[:log_dir],
              :prearchive_dir             => prearchive_dir,
              :archive_dir                => archive_dir,
              :logrotate_type             => params[:type]
    owner "root"
    group "root"
    mode 0644
    action :create
 end
end
