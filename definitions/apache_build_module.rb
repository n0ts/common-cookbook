#
# Cookbook Name:: common
# Definition:: apache_build_module
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#
#

define :common_apache_build_module, :conf => false, :conf_params => {} do
  include_recipe "apache2"
  params[:filename] = params[:filename] || "mod_#{params[:name]}.so"
  params[:module_path] = params[:module_path] || "#{node['apache']['libexecdir']}/#{params[:filename]}"
  params[:cookbook] = params[:cookbook] || "apache2"

  #
  # apxs2 command
  #
  package "pkg-httpd-devel" do
    case node.platform_family
    when "rhel"
      package_name "httpd-devel"
    when "debian"
      package_name "apache2-threaded-dev"
    end

    action :install
  end

  #
  # apache module source
  #
  cookbook_file "#{Dir.tmpdir}/mod_#{params[:name]}.c" do
    if params[:cookbook]
      cookbook params[:cookbook]
    end
    source "mod_#{params[:name]}.c"
    action :create
  end

  apxs2_cmd =
    case node.platform_family
    when "rhel"
      "apxs"
    else
      "apxs2"
    end
  execute "install-mod_#{params[:name]}" do
    command "#{apxs2_cmd} -a -i -c #{Dir.tmpdir}/mod_#{params[:name]}.c"
    user "root"
    action :run
    not_if do ::File.exists?(params[:module_path]) end
    notifies :run, "execute[uninstall-mod_#{params[:name]}]"
  end

  execute "uninstall-mod_#{params[:name]}" do
    command "rm -f #{Dir.tmpdir}/mod_#{params[:name]}.*"
    user "root"
    action :nothing
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

  file "#{node['apache']['dir']}/mods-available/#{params[:name]}.load" do
    content "LoadModule #{params[:name]}_module #{params[:module_path]}\n"
    mode 0644
  end

  apache_module params[:name] do
    enable true
    conf false
    if params[:cookbook]
      cookbook params[:cookbook]
    end
  end
end
