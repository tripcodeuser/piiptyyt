#!/usr/bin/perl -w
use strict;

use IO::File;
use Net::OAuth;
use CGI;

$Net::OAuth::PROTOCOL_VERSION = Net::OAuth::PROTOCOL_VERSION_1_0A;

my ($conkey, $consec);

sub slurp {
	my $fn = shift;
	my $fh = IO::File->new("< $fn") || die "can't open `$fn': $!";
	my $data = <$fh>;
	$fh->close;
	return $data;
}

sub request_token {
	my $q = shift;
	my $req_hash = shift;

	my $request = Net::OAuth->request('request token')->new(
		%$req_hash,
		request_url => "https://localhost/cgi-bin/oauth.cgi/request_token",
		request_method => $q->request_method,
		consumer_secret => $consec,
		callback => $q->param('oauth_callback') || '');
	if(!$request->verify) {
		print STDERR "correct signature: " . $request->signature . "\n";
		print STDERR "correct sigbase: " . $request->signature_base_string . "\n";
		print STDERR "correct sigkey: " . $request->signature_key . "\n";
		print $q->header(-status => 401);
	} else {
		my $resp = Net::OAuth->response('request token')->new(
			token => 'abcdef',
			token_secret => '0123456',
			callback_confirmed => 'true');
		print $q->header('application/x-www-form-urlencoded');
		print $resp->to_post_body;
	}
}

sub access_token {
	my $q = shift;
	my $req_hash = shift;

	my $request = Net::OAuth->request('access token')->new(
		%$req_hash,
		request_url => "https://localhost/cgi-bin/oauth.cgi/access_token",
		request_method => $q->request_method,
		consumer_secret => $consec,
		token_secret => '0123456');
	if(!$request->verify) {
		print STDERR "correct signature: " . $request->signature . "\n";
		print STDERR "correct sigbase: " . $request->signature_base_string . "\n";
		print STDERR "correct sigkey: " . $request->signature_key . "\n";
		print $q->header(-status => 401);
	} elsif($req_hash->{token} ne 'abcdef'
		|| $req_hash->{verifier} ne '12345')
	{
		print STDERR "invalid parameters.\n";
		print $q->header(-status => 400);
	} else {
		my $resp = Net::OAuth->response('access token')->new(
			token => 'huaglahgalhgalhgah',
			token_secret => '0xdeadbeef',
			extra_params => { screen_name => 'cheese', user_id => 666 });
		print $q->header('application/x-www-form-urlencoded');
		print $resp->to_post_body;
	}
}

my $path = "/tmp";
$conkey = slurp("$path/api.twitter.com__consumer_key");
chomp $conkey;
$consec = slurp("$path/api.twitter.com__consumer_secret");
chomp $consec;


my $q = CGI->new;
my %req_hash;
my $auth = $q->http('Authorization');
if($auth) {
	# ENTIRELY UNTESTED, as apache doesn't pass Authorization to the cgi
	# script.
	print STDERR "auth header: $auth\n";
	if($auth =~ /^OAuth /) {
		for(split /, ?/, $auth) {
			my ($name, $value) = /^(.+)="(.*)"$/;
			$req_hash{$name} = $value;
			print STDERR "oauth parameter `$name' -> `$value'\n";
		}
	}
} elsif($q->param('oauth_signature')) {
	# a real oauth script would restrict these somehow. meh.
	for($q->Vars) {
		next unless /^oauth_(.+)$/;
		$req_hash{$1} = $q->param($_);
	}
} else {
	print STDERR "POSTDATA is: `" . $q->param('POSTDATA') . "'\n";
	die "no authorization header or oauth_signature parameter";
}

my $path_info = $q->path_info;
if($q->path_info eq '/request_token') {
	request_token($q, \%req_hash);
} elsif($q->path_info eq '/access_token') {
	access_token($q, \%req_hash);
} else {
	print $q->header(-status => 404);
}
