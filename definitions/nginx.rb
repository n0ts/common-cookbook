#
# Cookbook Name:: common
# Definition:: nginx
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_nginx_site_conf, :enable => true, :timing => :delayed, :cookbook => "common" do
  include_recipe "nginx::default"

  template "#{node['nginx']['dir']}/sites-available/#{params[:name]}" do
    source params[:template]
    mode 0644
    if params[:cookbook]
      cookbook params[:cookbook]
    end
    variables(
              :params => params
              )
    if ::File.exists?("#{node['nginx']['dir']}/sites-enabled/#{params[:name]}")
      notifies :reload, "service[nginx]", params[:timing]
    end
  end

  nginx_site params[:name] do
    enable params[:enable]
    timing params[:timing]
  end
end
