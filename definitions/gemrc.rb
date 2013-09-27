#
# Cookbook Name:: common
# Definition:: gemrc
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_gemrc, :users => [] do
  if node[:os] == "linux"
    params[:users] += [ "root" ]
    params[:users].uniq.each do |user|
      home_dir = (user == "root") ? "/root" : "/home/#{user}"
      file "#{home_dir}/.gemrc" do
        content "gem: --no-document"
        owner user
        group user
        mode 0644
      end
    end
  end
end
