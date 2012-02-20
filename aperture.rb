require 'rubygems'
require 'plist'

path = 'public/Photos/Aperture Library.aplibrary'
album = 75
ap = Plist::parse_xml(path + '/ApertureData.xml')
if !ap then
  # test files
  puts "Final.jpg"
  puts "Final2.jpg"
  exit -1;
end
lib = ap['Archive Path'];

#puts "Application Version: #{ap['Application Version']}"
#puts "Library Path: #{lib}"

#puts "Album Count: #{ap['List of Albums'].length}"
#puts "Image Count: #{ap['Master Image List'].length}"
#puts "Faces Count: #{ap['List of Faces'].length}"
#puts "Project Count: #{ap['Project List'].length}"

#puts "Projects:"
#ap['Project List'].each{|p|
#  puts "-- #{p['ProjectName']} has #{p['NumImages']} images"
#}

#puts "Faces:"
#ap['List of Faces'].each{|k,v|
#  puts "-- #{v['name']}"
#}

def GetAlbum(ap, id)
  ap['List of Albums'].each{|a|
    return a if(a['AlbumId'] == id)
  }
  return nil
end

def GetPicture(ap, key)
  return ap['Master Image List'][key]
end

# puts "Albums:"
# ap['List of Albums'].each{|a|
#   imageCount = ''
#   parent = ''
#   if(a['KeyList']) then
#     imageCount = " has #{a['KeyList'].length} images"
#   end
#   if(a['Parent']) then
#     p = GetAlbum(ap, a['Parent'])
#     if(p) then
#       parent = " (parent #{p['AlbumName']})"
#     end
#   end
#   puts "-- #{a['AlbumName']} (#{a['AlbumId']}) #{imageCount}#{parent}"
# }

a = GetAlbum(ap, album)
if a['KeyList'] then
  imageCount = a['KeyList'].length
  #puts "#{imageCount}"
  a['KeyList'].each{|k|
    p = GetPicture(ap, k)
    next if !p
    image = p['ImagePath']
    image.gsub!(lib, '')
    image.gsub!(/^\//, '')
    image = path + '/' + image
    #puts "#{p['Caption']} -> #{image}"
    puts image
  }
end
#Master Image List

