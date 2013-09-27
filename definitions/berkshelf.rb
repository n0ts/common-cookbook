#
# Cookbook Name:: common
# Definition:: berkshelf
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_berkshelf do
  %w{ libxml2-dev libxslt-dev }.each do |pkg|
    package pkg do
      action :install
    end
  end

  rbenv_gem "berkshelf" do
    ruby_version params[:name]
  end
end
