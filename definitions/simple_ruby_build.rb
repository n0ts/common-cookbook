#
# Cookbook Name:: common
# Definition:: simple_ruby_build
#
# Copyright 2013, Naoya Nakazawa
#
# All rights reserved - Do Not Redistribute
#

define :common_extract_simple_ruby_build, :aws_region => "ap-northeast-1", :aws_s3_bucket => "simple-ruby-build", :install_prefix => "/opt", :local_owner => "root", :local_group => "root" do
  rbenv_tarball = "rbenv-#{params[:name]}.tar.gz"
  bash "copy-rbenv-from-s3" do
    user "root"
    group "root"
    environment ({ "AWS_DEFAULT_REGION" => params[:aws_region] })
    cwd params[:install_prefix]
    code <<-EOH
    aws s3 cp \
      s3://#{params[:aws_s3_bucket]}/#{rbenv_tarball} \
      #{params[:install_prefix]}/
EOH
    not_if { ::File.exist?("#{params[:install_prefix]}/#{rbenv_tarball}") }
  end

  bash "extract-simple-ruby-build" do
    user "root"
    group "root"
    cwd params[:install_prefix]
    code <<-EOH
    tar zxf #{params[:install_prefix]}/#{rbenv_tarball}
    chown -R #{params[:local_owner]}:#{params[:local_group]} \
      #{params[:install_prefix]}/rbenv
EOH
    not_if { ::FileTest.exist?("#{params[:install_prefix]}/rbenv") }
  end

  file "/etc/profile.d/rbenv.sh" do
    owner "root"
    group "root"
    mode 0755
    content <<-EOH
export RBENV_ROOT=#{params[:install_prefix]}/rbenv
export PATH=$RBENV_ROOT/bin:#{params[:install_prefix]}/rbenv/plugins/ruby_build/bin:$PATH

case $SHELL in
*/zsh)
  source $RBENV_ROOT/completions/rbenv.zsh
  ;;
*/bash)
  source $RBENV_ROOT/completions/rbenv.bash
  ;;
*)
  source $RBENV_ROOT/completions/rbenv.bash
  ;;
esac

eval "$(rbenv init -)"
EOH
  end
end
