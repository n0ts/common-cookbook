#
# Cookbook Name:: common
# Definition:: apache_module
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#
#

define :common_apache_module, :enable => true, :conf => false, :conf_params => {} do
  apache_module params[:name] do
    enable params[:enable]
    conf false
  end

  if params[:conf]
    template "#{node['apache']['dir']}/mods-available/#{params[:name]}.conf" do
      if params[:cookbook]
        cookbook params[:cookbook]
      end
      source "mods/#{params[:name]}.conf.erb"
      notifies :restart, "service[apache2]"
      variables params[:conf_params]
      mode 0644
    end
  end
end

