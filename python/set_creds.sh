`cat ~/.aws/credentials | awk 'NR==2{print "export AWS_ACCESS_KEY_ID=" $3}'`
`cat ~/.aws/credentials | awk 'NR==3{print "export AWS_SECRET_ACCESS_KEY=" $3}'`
`cat ~/.aws/credentials | awk 'NR==4{print "export AWS_SESSION_TOKEN=" $3}'`
`cat ~/.aws/credentials | awk 'NR==5{print "export AWS_SECURITY_TOKEN=" $3}'`
export AWS_DEFAULT_REGION="us-east-1"
