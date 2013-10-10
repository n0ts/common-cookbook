#
# Cookbook Name:: common
# Definition:: profile
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_profile_keychain, :user => "root", :group => "root" do
  common_profile "keychain" do
    user params[:user]
    group params[:group]
    content <<-EOH
if [ -x "`which keychain`" ]; then
  HOSTNAME=`hostname`
  keychain --quiet $HOME/.ssh/id_rsa
  [ -f ~/.keychain/$HOSTNAME-sh ] && source ~/.keychain/$HOSTNAME-sh
fi
EOH
  end
end


define :common_profile, :user => "root", :group => "root", :content => "" do
  file "/etc/profile.d/#{params[:name]}.sh" do
    owner "root"
    group "root"
    mode 0755
    content params[:content]
    action :create
  end
end
