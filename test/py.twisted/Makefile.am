#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.
#

TRIAL ?= trial

stubs: ../v0.16/ThriftTest.thrift ../SmallTest.thrift
	$(THRIFT) --gen py:twisted ../v0.16/ThriftTest.thrift
	$(THRIFT) --gen py:twisted ../SmallTest.thrift

check: stubs
	$(TRIAL) ./test_suite.py

clean-local:
	$(RM) -r build
	find . -type f \( -iname "*.pyc" \) | xargs rm -f
	find . -type d \( -iname "__pycache__" -or -iname "_trial_temp" \) | xargs rm -rf
	$(RM) -r gen-py*/

distdir:
	$(MAKE) $(AM_MAKEFLAGS) distdir-am

dist-hook:
	find $(distdir) -type f \( -iname "*.pyc" \) | xargs rm -f
	find $(distdir) -type d \( -iname "__pycache__" -or -iname "_trial_temp" \) | xargs rm -rf
	$(RM) -r $(distdir)/gen-py*/
