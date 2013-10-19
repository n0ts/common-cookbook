#
# Cookbook Name:: common
# Definition:: supervisor
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_supervisor_service, :user => "root" do
  execute "supervisorctl-restart" do
    command "supervisorctl restart #{params[:name]}"
    action :nothing
  end

  supervisor_service params[:name] do
    command params[:command]
    stdout_logfile params[:stdout_logfile]
    stdout_logfile_maxbytes "10MB"
    stdout_logfile_backups 10
    user params[:user]
    autostart true
    action :enable
    notifies :run, "execute[supervisorctl-restart]", :delayed
  end
end
