#
# Cookbook Name:: common
# Definition:: mysql
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_mysql_database, :write_user_name => "", :write_user_password => "", :read_user_name => "", :read_user_password => "" do

  include_recipe "database::mysql"

  mysql_connection_info = {
    :host     => "localhost",
    :username => "root",
    :password => node["mysql"]["server_root_password"],
  }

  mysql_database params[:name] do
    connection mysql_connection_info
    action :create
  end

  mysql_database_user params[:write_user_name] do
    connection mysql_connection_info
    password params[:write_user_password]
    database_name params[:name]
    host "%"
    privileges [
                "select", "insert", "update",
                "delete", "create", "create routine", "drop",
                "index", "lock tables", "alter", "alter routine",
               ]
    action :grant
    not_if { params[:write_user_name].empty? }
  end

  mysql_database_user params[:read_user_name] do
    connection mysql_connection_info
    password params[:read_user_password]
    host "%"
    privileges [
                "select",
               ]
    action :grant
    not_if { params[:read_user_name].empty? }
  end
end


define :common_mysql_nagios, :user_password => "nagios" do
  include_recipe "database::mysql"

  mysql_connection_info = {
    :host     => "localhost",
    :username => "root",
    :password => node["mysql"]["server_root_password"],
  }

  mysql_database_user params[:name] do
    connection mysql_connection_info
    password params[:user_password]
    host "%"
    privileges [
                "replication client",
               ]
    action :grant
  end
end
